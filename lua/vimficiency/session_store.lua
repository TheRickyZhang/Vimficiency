-- lua/vimficiency/session_store.lua
-- Manages sessions across two indexing layers, on top of one canonical
-- record store.
--
-- Storage:
--   session_records[id]    = SessionRecord  (one per session, ever)
--   manual_alias_to_id     = alias -> id    (manual handles)
--   recall_id_order        = id[]           (chronological recall ring)
--
-- A SessionRecord carries everything: start state, key buffer (while
-- active), result (after finish), status, key_count. There is no
-- separate active_sessions / result_sessions split. Finishing flips
-- record.status from "active" to "finished" and attaches result; it
-- does NOT remove the record from any index. That removes the
-- "tombstone in recall_id_order" class of bugs the prior split had.
--
-- Eviction (recall ring): union semantics. Drop the oldest record from
-- the ring only when both KEY_SESSION_CAPACITY AND MAX_RETENTION_SECONDS
-- say drop. Recall records are ephemeral: when a record falls out of
-- the ring (or when recall is disabled), it's destroyed entirely —
-- active or finished. Recall results are transient queries by design;
-- if the user wants to keep one, they `:Vimfy save <selector> [<name>]`
-- it to disk before it rotates out.

local M = {}

local alias_mod = require("vimficiency.alias")
local key_tracking = require("vimficiency.key_tracking")
local util = require("vimficiency.util")
local config = require("vimficiency.config")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

--- SessionRecord: the canonical, mutable record for one session over its
--- entire lifecycle. While `status == "active"`, key_seq accumulates and
--- result is nil. At finish, status flips to "finished", result is
--- attached, key_count is frozen, and key_seq is dropped (the compiled
--- `result.user_seq` is what's needed afterward).
---
--- ActiveSession is an alias for the same table while in the active phase
--- — kept in the public type system so existing callers (session.lua)
--- read naturally.
---
---@class SessionRecord
---@field id          string                    # Unique identifier
---@field key_nsid    integer                   # >=0 for manual w/ macro recording, -1 for recall (uses global tracking)
---@field win         integer                   # Window where session started
---@field buf         integer                   # Buffer where session started
---@field start_state VimficiencyState          # Cursor/viewport at start
---@field time_started integer                  # hrtime when started
---@field status      "active" | "finished"
---@field key_seq     VimficiencyKeyEvent[]|nil # Accumulated keys (active only); dropped at finish
---@field key_count   integer                   # Live count while active; frozen at finish
---@field first_mode  string|nil                # Vim mode of the first captured key (recall only); used for command-boundary snapping
---@field result      ResultSession|nil         # Set at finish
---@field finish_alias string|nil               # Literal alias passed to finish (`a`, `3s`, ...); used as the default filename for `:Vimfy save`
---@field start_kind  "manual"|"auto"           # Who picked the start. Manual = user called :Vimfy start|watch. Auto = created from the recall queue.
---@field end_kind    "manual"|"auto"           # Who triggers the end. Manual = user calls :Vimfy end. Auto = an end-trigger (idle, etc.) fires finish. Combined with start_kind, yields one of Mark/Watch/Recall/Suggest.
---@field watch_disarm fun()|nil                # Watch sessions only: disarm for the idle end-trigger armed at :Vimfy watch. Called on finish/destroy so the timer and global subscriber are released.

---@alias ActiveSession SessionRecord

--- ResultSession: data for a completed session, ready for simulate().
---@class ResultSession
---@field lines string[]               # Trimmed buffer lines used for optimization
---@field start_row integer            # 0-indexed, relative to lines
---@field start_col integer            # 0-indexed
---@field end_row integer              # 0-indexed, relative to lines
---@field end_col integer              # 0-indexed
---@field user_seq string              # What the user typed (keytrans string)
---@field user_cost number             # Effort cost of user's sequence
---@field optimal_results VimficiencyResult[] # Top N results from optimizer (seq + cost)
---@field start_time integer           # hrtime when the session started
---@field key_count integer            # Captured key events at finish (authoritative; user_seq is bytes, not keys)
---@field timestamp integer            # hrtime when the result was computed (finish time)

--- SessionSummary: normalized view-model used by `:Vimfy list`, auto-suggest
--- notifications, and the future session menu. One shape regardless of
--- whether the underlying session is still active or has a result.
---
--- `id` is the stable handle. Use it as the key for any follow-up
--- action on the session (finish, remove, resubscribe).
---
--- `display_alias` is for presentation only and, for recall sessions,
--- is **time-varying**: as the ring rotates, the same record's
--- "N keys ago" index slides. A summary captured now may print "5" and
--- be rendered a moment later when that slot is "7" or has rotated out
--- entirely. Never feed it back into a store call — use `id`.
---
--- `type` is the canonical 2×2 discriminator derived from
--- (`start_kind`, `end_kind`) via `session_type_from_kinds`. Callers
--- that branch on session semantics should switch on `type` so that
--- adding a fifth cell is a compile-time error (exhaustive check),
--- rather than reading the axes directly and silently missing a case.
---@class SessionSummary
---@field id            string
---@field type          SessionType    # Derived from (start_kind, end_kind). Switch on this for exhaustive dispatch.
---@field start_kind    "manual"|"auto"
---@field end_kind      "manual"|"auto"
---@field display_alias string|nil     # For display. Time-varying for recall; do not re-feed.
---@field status        "active" | "finished"
---@field start_time    integer        # hrtime
---@field end_time      integer|nil    # hrtime at finish; nil if still active
---@field key_count     integer        # Keystrokes captured so far
---@field preview       string         # First ~20 chars of user_seq for display
---@field result        ResultSession|nil

---@alias SessionType "mark" | "watch" | "recall" | "suggest"

--- Derive the canonical SessionType from the (start_kind, end_kind) axes.
--- This is the one place that encodes the 2×2 mapping; UI, tests, and
--- anything else that needs a single discriminator MUST route through
--- here rather than duplicate the table.
---@param start_kind "manual"|"auto"
---@param end_kind   "manual"|"auto"
---@return SessionType
function M.session_type_from_kinds(start_kind, end_kind)
  if start_kind == "manual" and end_kind == "manual" then return "mark" end
  if start_kind == "manual" and end_kind == "auto"   then return "watch" end
  if start_kind == "auto"   and end_kind == "manual" then return "recall" end
  if start_kind == "auto"   and end_kind == "auto"   then return "suggest" end
  error("invalid session kinds: (" .. tostring(start_kind) .. ", " .. tostring(end_kind) .. ")")
end

--------------------------------------------------------------------------------
-- Constructor
--------------------------------------------------------------------------------

---@param id string
---@param key_nsid integer  >= 0 for manual sessions (with macro recording), -1 for recall sessions
---@param win integer
---@param buf integer
---@param start_state VimficiencyState
---@param start_kind "manual"|"auto"
---@param end_kind "manual"|"auto"
---@return SessionRecord
function M.new_active_session(id, key_nsid, win, buf, start_state, start_kind, end_kind)
  assert(type(id) == "string" and id ~= "", "session.id must be nonempty string")
  assert(type(key_nsid) == "number", "key_nsid must be a number")
  assert(vim.api.nvim_win_is_valid(win), "win must be a valid window id")
  assert(vim.api.nvim_buf_is_valid(buf), "buf is not valid: " .. buf)
  assert(start_kind == "manual" or start_kind == "auto",
    "start_kind must be 'manual' or 'auto', got: " .. tostring(start_kind))
  assert(end_kind == "manual" or end_kind == "auto",
    "end_kind must be 'manual' or 'auto', got: " .. tostring(end_kind))

  return {
    id = id,
    key_nsid = key_nsid,
    win = win,
    buf = buf,
    start_state = start_state,
    time_started = vim.uv.hrtime(),
    status = "active",
    key_seq = {},
    key_count = 0,
    first_mode = nil,
    result = nil,
    start_kind = start_kind,
    end_kind = end_kind,
  }
end

--------------------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------------------

local MANUAL_CAPACITY = 5      -- concurrent active sessions; alias grammar is strict alphabetic (see alias.lua)

--------------------------------------------------------------------------------
-- Storage
--------------------------------------------------------------------------------

---@type table<string, SessionRecord>
local session_records = {}

---@type table<string, string>  -- alias -> id
local manual_alias_to_id = {}
local manual_count = 0

---@type string[]  -- ordered deque of recall ids (oldest first, newest last)
local recall_id_order = {}

---@type boolean
local recall_enabled = false

---@type string|nil  The id of the most recently finished session. Drives
---                   the `@` selector for `:Vimfy save`. Cleared when the
---                   record is destroyed (manual overwrite, recall
---                   eviction, recall disable).
local last_finished_id = nil

--------------------------------------------------------------------------------
-- Recall lookup
--------------------------------------------------------------------------------

--- Find the youngest recall record index `i` whose time_started <= target.
--- Reads time_started from records (not from active state), so finished
--- records in the ring are first-class — no tombstone misbehavior.
---@param target_hrtime integer  # in hrtime units (nanoseconds)
---@return integer|nil index  # 1-based index into recall_id_order, or nil if none
local function find_session_at_or_before(target_hrtime)
  local lo, hi = 1, #recall_id_order
  local best = nil
  while lo <= hi do
    local mid = math.floor((lo + hi) / 2)
    local id = recall_id_order[mid]
    local rec = session_records[id]
    if not rec then
      -- Genuinely missing record (defensive only — shouldn't happen).
      lo = mid + 1
    elseif rec.time_started <= target_hrtime then
      best = mid
      lo = mid + 1
    else
      hi = mid - 1
    end
  end
  return best
end

--- Does the recall record at this index start at a clean normal-mode
--- command boundary? Reads first_mode from the record so the check
--- works regardless of the session's status (active or finished).
--- Pure — takes explicit `records` and `order` tables so tests can
--- feed synthetic state without touching module-level storage.
---@param records table<string, SessionRecord>
---@param order string[]
---@param index integer
---@return boolean
local function is_clean_boundary_pure(records, order, index)
  local id = order[index]
  local rec = records[id]
  if not rec or not rec.first_mode then return false end
  local m = rec.first_mode
  if m:sub(1, 2) == "no" then return false end  -- operator-pending
  return m:sub(1, 1) == "n"
end

--- Snap a recall_time cutoff index backward to the nearest clean command
--- boundary, bounded by `budget`. Pure — see `is_clean_boundary_pure`.
---@param records table<string, SessionRecord>
---@param order string[]
---@param cutoff_index integer
---@param budget integer
---@return integer|nil index
local function snap_backward_to_boundary_pure(records, order, cutoff_index, budget)
  local i = cutoff_index
  local steps = 0
  while i >= 1 and steps <= budget do
    if is_clean_boundary_pure(records, order, i) then
      return i
    end
    i = i - 1
    steps = steps + 1
  end
  return nil
end

local function snap_backward_to_boundary(cutoff_index)
  return snap_backward_to_boundary_pure(
    session_records, recall_id_order, cutoff_index,
    config.SNAP_LOOKBACK_KEYS or 20
  )
end

--- Convert alias to session ID.
---@param alias string
---@return string|nil id
local function convert_alias_to_id(alias)
  local session_type, value = alias_mod.parse(alias)
  if not session_type then return nil end

  if session_type == "manual" then
    return manual_alias_to_id[alias]
  end

  if session_type == "recall_key" then
    local index = #recall_id_order - value + 1
    if index >= 1 and index <= #recall_id_order then
      return recall_id_order[index]
    end
    return nil
  end

  -- recall_time: `Ns`
  local target = vim.uv.hrtime() - value * 1e9

  -- Honest failure: if no retained record is at-or-before the target,
  -- the queue doesn't cover N seconds. Return nil so the caller can
  -- surface "No recall session found within 'Ns'…" (see docs/user/05-
  -- recall.md). A silent fallback to the oldest record would hand back
  -- a window much shorter than N and make N meaningless.
  local cutoff_index = find_session_at_or_before(target)
  if not cutoff_index then return nil end

  local snapped = snap_backward_to_boundary(cutoff_index)
  if not snapped then return nil end
  return recall_id_order[snapped]
end

--------------------------------------------------------------------------------
-- Internal mutators
--------------------------------------------------------------------------------

--- Detach key tracking for a record (manual sessions only) and drop the
--- record from session_records.
---@param id string
---@return boolean removed
local function destroy_record(id)
  local rec = session_records[id]
  if not rec then return false end
  if rec.watch_disarm then
    rec.watch_disarm()
    rec.watch_disarm = nil
  end
  if rec.key_nsid and rec.key_nsid >= 0 then
    key_tracking.detach(rec.key_nsid)
  end
  session_records[id] = nil
  if last_finished_id == id then last_finished_id = nil end
  return true
end

--- Splice an id out of recall_id_order, if present. O(N) scan; called
--- only on explicit removal or eviction.
---@param id string
local function unindex_recall(id)
  for i = 1, #recall_id_order do
    if recall_id_order[i] == id then
      table.remove(recall_id_order, i)
      return
    end
  end
end

--- Union-semantic eviction: drop oldest recall ids ONLY when both the
--- count AND the age exceed their respective caps. A record evicted
--- from the ring is destroyed entirely, regardless of status.
local function evict_old_recall()
  local now = vim.uv.hrtime()
  local max_age_ns = config.MAX_RETENTION_SECONDS * 1e9
  while #recall_id_order > 0 do
    local oldest_id = recall_id_order[1]
    local rec = session_records[oldest_id]
    if not rec then
      table.remove(recall_id_order, 1)
    else
      local age_ns = now - rec.time_started
      local count_would_drop = #recall_id_order > config.KEY_SESSION_CAPACITY
      local age_would_drop = age_ns > max_age_ns
      if count_would_drop and age_would_drop then
        table.remove(recall_id_order, 1)
        destroy_record(oldest_id)
      else
        break
      end
    end
  end
end

--------------------------------------------------------------------------------
-- Public API
--------------------------------------------------------------------------------

--- Check if we can store a manual session.
--- Call this BEFORE allocating resources (key_nsid) to avoid cleanup on failure.
---@param alias string
---@return boolean can_store
function M.can_store_manual(alias)
  if manual_alias_to_id[alias] then return true end  -- overwrite always allowed
  return manual_count < MANUAL_CAPACITY
end

--- Store a manual session.
--- Caller must ensure can_store_manual(alias) returned true.
---@param alias string
---@param record SessionRecord
---@return boolean overwrote  True if an existing session was replaced
function M.store_manual(alias, record)
  local existing_id = manual_alias_to_id[alias]

  if existing_id then
    destroy_record(existing_id)
  else
    manual_count = manual_count + 1
  end

  manual_alias_to_id[alias] = record.id
  session_records[record.id] = record
  return existing_id ~= nil
end

--- Store a recall session at the newest position, evicting if caps demand.
---@param record SessionRecord
function M.store_recall(record)
  session_records[record.id] = record
  table.insert(recall_id_order, record.id)
  evict_old_recall()
end

---@param alias string
---@return SessionRecord|nil
function M.get_active(alias)
  local id = convert_alias_to_id(alias)
  if not id then return nil end
  local rec = session_records[id]
  if rec and rec.status == "active" then return rec end
  return nil
end

---@param alias string
---@return ResultSession|nil
function M.get_result(alias)
  local id = convert_alias_to_id(alias)
  if not id then return nil end
  local rec = session_records[id]
  return rec and rec.result or nil
end

---@param alias string
---@return boolean
function M.has_active(alias)
  return M.get_active(alias) ~= nil
end

---@param alias string
---@return boolean
function M.has_result(alias)
  return M.get_result(alias) ~= nil
end

--- Remove a session entirely (record + indices) by stable id.
---
--- Takes an id rather than an alias on purpose: time-varying aliases
--- (`3s`, recall key `N`) resolve to different records as the ring
--- rotates. Callers must resolve alias → record via `get_active` at
--- the entry point and pass `record.id` here. Resolving twice risks
--- removing the wrong record if the ring slides between calls.
---@param id string
function M.remove(id)
  -- Guard against the historical mistake of passing an alias. Aliases
  -- are time-varying (a recall `3s` resolves to a different record as
  -- the ring rotates) — removing by alias risks destroying the wrong
  -- session. `util.is_session_id` owns the id-vs-alias discrimination
  -- and sits next to `util.new_id`, so if the id format ever changes
  -- the detector changes in the same edit. Callers must resolve
  -- `get_active(alias).id` first.
  assert(util.is_session_id(id),
    "session_store.remove requires a session id (from util.new_id), not an alias: "
    .. tostring(id))

  local rec = session_records[id]
  if not rec then return end

  -- Un-index from whichever side holds this id.
  local manual_alias = nil
  for a, mid in pairs(manual_alias_to_id) do
    if mid == id then manual_alias = a; break end
  end
  if manual_alias then
    manual_alias_to_id[manual_alias] = nil
    manual_count = manual_count - 1
  else
    unindex_recall(id)
  end

  destroy_record(id)
end

--- Transition an in-progress session to finished. Detaches per-session
--- key tracking (manual macro recording). Does NOT remove the record
--- from any index — finished records remain reachable for replay.
---
--- Takes an id rather than an alias on purpose: time-varying aliases
--- (`3s`, recall key `N`) resolve to different records as the ring
--- rotates. Callers must resolve alias → record via `get_active` at
--- the entry point and pass `record.id` here. Resolving twice risks
--- finishing the wrong record — and attaching the result to a record
--- that wasn't even the one the result was computed from.
---@param id string
---@param result ResultSession
---@param finish_alias string|nil       Literal alias the caller used for this finish (`a`, `3s`, etc.); stored for `:Vimfy save @` default naming
---@param end_kind_override "manual"|"auto"|nil  When non-nil, atomically update rec.end_kind as part of the same status transition. Used by auto_suggest to promote a Recall record (`auto, manual`) to Suggest (`auto, auto`) only on confirmed finish — a speculative mutation before finish would leave a failed compute/finish path with a mislabeled record.
---@return boolean success
function M.finish_session(id, result, finish_alias, end_kind_override)
  local rec = session_records[id]
  if not rec or rec.status ~= "active" then return false end

  assert(end_kind_override == nil or end_kind_override == "manual" or end_kind_override == "auto",
    "end_kind_override must be 'manual', 'auto', or nil; got: " .. tostring(end_kind_override))

  if rec.watch_disarm then
    rec.watch_disarm()
    rec.watch_disarm = nil
  end

  if rec.key_nsid and rec.key_nsid >= 0 then
    key_tracking.detach(rec.key_nsid)
  end

  if end_kind_override then
    rec.end_kind = end_kind_override
  end
  rec.status = "finished"
  rec.result = result
  rec.key_count = #(rec.key_seq or {})
  rec.key_seq = nil  -- compiled into result.user_seq; free the events
  rec.finish_alias = finish_alias
  last_finished_id = id

  return true
end

--- Resolve the `@` selector to the most recently finished result.
--- Returns nil if no session has finished yet, or if the most recent
--- finished record has since been destroyed.
---@return ResultSession|nil
function M.get_last_finished_result()
  if not last_finished_id then return nil end
  local rec = session_records[last_finished_id]
  return rec and rec.result or nil
end

--- Literal alias the user passed when the most recent session was
--- finished (`a`, `3s`, `5`). Used by `:Vimfy save @` to derive a
--- default filename. Returns nil when nothing has finished yet, when
--- the record has been destroyed, or when the caller didn't pass an
--- alias to `finish_session`.
---@return string|nil
function M.get_last_finished_alias()
  if not last_finished_id then return nil end
  local rec = session_records[last_finished_id]
  return rec and rec.finish_alias or nil
end

--- List all valid aliases for debugging / tab-completion.
---@return string[]
function M.list_aliases()
  local aliases = {}

  for alias, _ in pairs(manual_alias_to_id) do
    table.insert(aliases, alias)
  end

  for i = 1, #recall_id_order do
    table.insert(aliases, tostring(i))
  end

  return aliases
end

--------------------------------------------------------------------------------
-- Session summary (normalized view-model)
--------------------------------------------------------------------------------

---@param id string
---@return integer|nil index
local function find_recall_index(id)
  for i = 1, #recall_id_order do
    if recall_id_order[i] == id then return i end
  end
  return nil
end

---@param id string
---@return string|nil alias
local function find_manual_alias(id)
  for a, mid in pairs(manual_alias_to_id) do
    if mid == id then return a end
  end
  return nil
end

--- Build a SessionSummary for the session with this id. Returns nil if
--- the id isn't known to the store.
---@param id string
---@return SessionSummary|nil
function M.summarize(id)
  local rec = session_records[id]
  if not rec then return nil end

  local manual_alias = find_manual_alias(id)
  local recall_index = find_recall_index(id)

  local display_alias
  if manual_alias then
    display_alias = manual_alias
  elseif recall_index then
    -- Recall aliases are "N keys from newest"; drift as the ring
    -- rotates. This is presentation-only — see the SessionSummary
    -- docstring. Any follow-up action must key on `id`, not this
    -- string.
    display_alias = tostring(#recall_id_order - recall_index + 1)
  end
  -- Defensive: if neither index knows this id, display_alias stays
  -- nil. Under current wiring this branch is unreachable.

  local result = rec.result
  local user_seq = (result and result.user_seq) or ""
  local preview = user_seq:sub(1, 20)
  if #user_seq > 20 then preview = preview .. "…" end

  return {
    id            = id,
    type          = M.session_type_from_kinds(rec.start_kind, rec.end_kind),
    start_kind    = rec.start_kind,
    end_kind      = rec.end_kind,
    display_alias = display_alias,
    status        = rec.status,
    start_time    = rec.time_started,
    end_time      = result and result.timestamp or nil,
    key_count     = (rec.status == "active") and #(rec.key_seq or {}) or rec.key_count,
    preview       = preview,
    result        = result,
  }
end

--- Summarize every session the store knows about. Order is unspecified;
--- callers that care should sort.
---@return SessionSummary[]
function M.summarize_all()
  local out = {}
  for id, _ in pairs(session_records) do
    local s = M.summarize(id)
    if s then table.insert(out, s) end
  end
  return out
end

--------------------------------------------------------------------------------
-- Recall enable/disable
--------------------------------------------------------------------------------

---@return boolean
function M.is_recall_enabled()
  return recall_enabled
end

--- Enable the rolling recall ring. Every keystroke creates a new retained
--- record (bounded by KEY_SESSION_CAPACITY and MAX_RETENTION_SECONDS under
--- union semantics) and appends the key to all still-active retained
--- records.
---@return boolean success
function M.enable_recall()
  if recall_enabled then
    return false
  end

  local function on_key_event(event)
    -- 1. Append key event to all still-active recall records. Finished
    --    records are skipped (their key_seq is nil and they're frozen).
    --
    -- This fanout is O(n) per keystroke in the number of live recall
    -- records. n is bounded by KEY_SESSION_CAPACITY (= 200) and each
    -- append is a table_insert of a small event table, so real cost is
    -- well under 1 ms per key at typing speeds. If capacity ever grows
    -- (or we retain more aggressively), switch to a shared ring + per-
    -- record {start_idx, end_idx} slice view: each record holds two
    -- offsets into a single shared event buffer and append becomes O(1).
    for _, id in ipairs(recall_id_order) do
      local rec = session_records[id]
      if rec and rec.status == "active" then
        table.insert(rec.key_seq, event)
        rec.key_count = rec.key_count + 1
      end
    end

    -- 2. Create new record with current state (BEFORE the key is processed).
    -- vim.on_key fires before the key is processed, so capture_state returns
    -- the position before this key takes effect.
    local buf = event.buf
    local win = event.win

    if not vim.api.nvim_buf_is_valid(buf) or not vim.api.nvim_win_is_valid(win) then
      return
    end

    local id = util.new_id(buf)
    local start_state = util.capture_state(buf, win)

    -- Recall: auto start, manual end. Suggest is created by flipping
    -- end_kind to "auto" inside auto_suggest.lua at takeover time.
    local rec = M.new_active_session(id, -1, win, buf, start_state, "auto", "manual")

    -- Include current key in new record since state is captured BEFORE key
    -- is processed. The first event's mode drives command-boundary snapping
    -- for `end Ns` queries — capture it so the check survives finish().
    table.insert(rec.key_seq, event)
    rec.key_count = 1
    rec.first_mode = event.mode

    M.store_recall(rec)
  end

  local success = key_tracking.attach_global(on_key_event)
  if success then
    recall_enabled = true
  end
  return success
end

--- Disable the recall ring. Drops all recall records (active and
--- finished) — recall results are transient by design. The user can
--- promote a finished result into disk via `:Vimfy save <alias> [<name>]`
--- before disabling if they want to keep it.
function M.disable_recall()
  if not recall_enabled then
    return
  end

  key_tracking.detach_global()
  recall_enabled = false

  for i = #recall_id_order, 1, -1 do
    destroy_record(recall_id_order[i])
  end
  recall_id_order = {}
end

-- Test-only exports of pure helpers. Not part of the public API; the
-- underscore prefix is intentional. Used by tests/lua/test_recall_snap.lua
-- to feed synthetic ring state through the snap algorithm without
-- touching module-level storage.
M._pure = {
  snap_backward_to_boundary = snap_backward_to_boundary_pure,
}

return M
