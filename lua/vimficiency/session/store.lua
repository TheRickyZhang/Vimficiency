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

local alias_mod = require("vimficiency.session.alias")
local ffi_lib = require("vimficiency.ffi")
local key_tracking = require("vimficiency.capture.key_tracking")
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

---@alias FinishReason
---| "manual"        # `:Vimfy end` (mark or recall)
---| "watch_idle"    # watch session's idle trigger fired
---| "suggest_idle"  # auto_suggest.idle fired
---| "suggest_keys"  # auto_suggest.keys fired
---| "suggest_cost"  # auto_suggest.cost fired

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
---@field finish_reason FinishReason   # Why the session ended (absent on pre-reason saved files)

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

local MANUAL_CAPACITY = 5      -- concurrent active sessions. Finished records retain their alias slot (for `save @` / `view <alias>`) but do not count against this cap. Alias grammar is strict alphabetic (see alias.lua).

--------------------------------------------------------------------------------
-- Storage
--------------------------------------------------------------------------------

---@type table<string, SessionRecord>
local session_records = {}

---@type table<string, string>  -- alias -> id
local manual_alias_to_id = {}

---@type string[]  -- ordered deque of recall ids (oldest first, newest last)
local recall_id_order = {}

---@type string|nil  The id of the most recently finished session. Drives
---                   the `@` selector for `:Vimfy save`. Cleared when the
---                   record is destroyed (manual overwrite, recall
---                   eviction, recall disable).
local last_finished_id = nil

--------------------------------------------------------------------------------
-- Recall lookup
--------------------------------------------------------------------------------

--- Find the youngest recall record index `i` whose time_started <= target.
---@param records table<string, SessionRecord>
---@param order string[]
---@param target_hrtime integer
---@param budget integer
---@return integer|nil
local function resolve_recall_cutoff_pure(records, order, target_hrtime, budget)
  return ffi_lib.resolve_recall_cutoff(records, order, target_hrtime, budget)
end

---@param cutoff_index integer
---@return integer|nil
local function snap_backward_to_boundary_pure(records, order, cutoff_index, budget)
  local synthetic = {}
  for i = 1, #order do
    local id = order[i]
    local rec = records[id] or {}
    synthetic[id] = {
      time_started = rec.time_started or i,
      first_mode = rec.first_mode,
    }
  end
  return resolve_recall_cutoff_pure(synthetic, order, cutoff_index, budget)
end

---@param target_hrtime integer
---@return integer|nil
local function resolve_recall_cutoff(target_hrtime)
  return resolve_recall_cutoff_pure(
    session_records,
    recall_id_order,
    target_hrtime,
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
  -- surface "No recall session found within 'Ns'…" (see doc-src/05-
  -- recall.md). A silent fallback to the oldest record would hand back
  -- a window much shorter than N and make N meaningless.
  local snapped = resolve_recall_cutoff(target)
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
--- The cap is on concurrent *active* sessions — finished records still
--- hold their alias slot (so `:Vimfy save @` / `:Vimfy view <alias>`
--- keep working) but don't block a new start. At N≤5 we just scan.
---@param alias string
---@return boolean can_store
function M.can_store_manual(alias)
  if manual_alias_to_id[alias] then return true end  -- overwrite/replace always allowed
  local active_count = 0
  for _, id in pairs(manual_alias_to_id) do
    local rec = session_records[id]
    if rec and rec.status == "active" then
      active_count = active_count + 1
    end
  end
  return active_count < MANUAL_CAPACITY
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

--- id-keyed variant of `get_result`. Use this when the caller already has
--- a resolved id and needs to look up the record WITHOUT re-resolving an
--- alias — critical for `:Vimfy store`, where the selector (possibly a
--- time-varying `Ns`) is resolved once at entry and then used for both
--- the result lookup and the subsequent `remove`, eliminating the risk
--- that a ring-boundary crossing during the blocking disk write causes
--- the alias to drift to a neighboring record.
---@param id string
---@return ResultSession|nil
function M.get_result_by_id(id)
  if not util.is_session_id(id) then return nil end
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
---@param reason FinishReason           Why the session ended; stored on the result for display and debugging.
---@return boolean success
function M.finish_session(id, result, finish_alias, end_kind_override, reason)
  local rec = session_records[id]
  if not rec or rec.status ~= "active" then return false end

  assert(end_kind_override == nil or end_kind_override == "manual" or end_kind_override == "auto",
    "end_kind_override must be 'manual', 'auto', or nil; got: " .. tostring(end_kind_override))
  assert(reason == "manual" or reason == "watch_idle"
      or reason == "suggest_idle" or reason == "suggest_keys" or reason == "suggest_cost",
    "reason must be a FinishReason; got: " .. tostring(reason))

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
  result.finish_reason = reason
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

--- Resolve an alias to its session id. Useful for callers that need to
--- cross-reference the two id-bearing APIs (`remove`, `finish_session`)
--- from an alias they received externally.
---
--- Returns nil for unknown aliases and for the `@` shorthand (callers
--- that want the last-finished id should use `get_last_finished_id`).
---@param alias string
---@return string|nil id
function M.get_id(alias)
  return convert_alias_to_id(alias)
end

--- Id of the most recently finished session (whatever alias it was
--- attached to). Returns nil if nothing has finished or the record has
--- since been destroyed.
---@return string|nil id
function M.get_last_finished_id()
  if not last_finished_id then return nil end
  if not session_records[last_finished_id] then return nil end
  return last_finished_id
end

--- Insert a result loaded from disk as a finished session under `alias`.
---
--- Backs `:Vimfy fetch` and the implicit fetch-on-`sim` for disk-only names.
--- The resulting record looks like any other finished session — subsequent
--- `get_result(alias)`, `:Vimfy sim alias`, `:Vimfy save alias as ...`, etc.
--- all work.
---
--- Refuses (returns nil + reason) if the alias is already in use, because
--- the workspace-vs-storage model says fetching into an occupied alias is
--- an error, not a silent overwrite.
---
---@param alias string  Must satisfy `alias.is_valid_manual`.
---@param result ResultSession
---@return string|nil id
---@return string|nil err
function M.register_fetched_result(alias, result)
  if not alias_mod.is_valid_manual(alias) then
    return nil, "alias '" .. tostring(alias) .. "' is not a valid manual alias (alphabetic only)"
  end
  if manual_alias_to_id[alias] then
    return nil, "alias '" .. alias .. "' is already in use in the current session"
  end
  assert(type(result) == "table", "result must be a table")

  -- We need a valid buf for `util.new_id` (it uses the buffer name to
  -- derive the id prefix). Current buffer works — the id is opaque and
  -- doesn't imply the record is attached to that buffer.
  local cur_buf = vim.api.nvim_get_current_buf()
  local id = util.new_id(cur_buf)

  session_records[id] = {
    id            = id,
    key_nsid      = -1,   -- no key tracking for fetched records
    win           = vim.api.nvim_get_current_win(),
    buf           = cur_buf,
    start_state   = nil,  -- not used for already-finished records
    time_started  = vim.uv.hrtime(),
    status        = "finished",
    key_seq       = nil,
    key_count     = (result.key_count or 0),
    first_mode    = nil,
    result        = result,
    start_kind    = "manual",
    end_kind      = "manual",
    finish_alias  = alias,
  }
  manual_alias_to_id[alias] = id
  last_finished_id = id
  return id, nil
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
-- Recall install
--------------------------------------------------------------------------------

--- Ingest one tracked keystroke into the rolling recall ring. Appends the
--- event to all still-active recall records, then opens a new auto-start /
--- manual-end record rooted at the pre-key state.
---
--- Callers are responsible for event-source filtering and window validity.
---@param event VimficiencyKeyEvent
function M.ingest_recall_event(event)
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

--- Strip pre-resolution pending events from every active recall record.
--- Called by the global on_key listener when it sees a multi-key mapping
--- resolution event — the individual LHS keys already reached
--- `ingest_recall_event` and must be retroactively removed so recall
--- sessions don't record the mapping's LHS as motion. Walks back over
--- each record's `key_seq` and pops entries whose `key_typed_raw`
--- concatenates to `typed_raw`. A mismatch (e.g., the record's tail has
--- already been rotated out) is silently skipped per-record.
---@param typed_raw string   The resolution event's raw `typed` (full LHS bytes).
function M.strip_recall_pre_resolution(typed_raw)
  for _, id in ipairs(recall_id_order) do
    local rec = session_records[id]
    if rec and rec.status == "active" and rec.key_seq then
      local popped = key_tracking.strip_matching_tail(rec.key_seq, typed_raw)
      if popped > 0 then
        rec.key_count = math.max(0, (rec.key_count or 0) - popped)
      end
    end
  end
end

-- Test-only exports of pure helpers. Not part of the public API; the
-- underscore prefix is intentional. Used by tests/lua/recall_snap.lua
-- to feed synthetic ring state through the snap algorithm without
-- touching module-level storage.
M._pure = {
  resolve_recall_cutoff = resolve_recall_cutoff_pure,
  snap_backward_to_boundary = snap_backward_to_boundary_pure,
}

--- Test-only: fetch an active record by id. Public callers go through
--- `get_active(alias)` which looks up by alias; tests need by-id access
--- to inspect internal state (key_seq, key_count) after seeding.
---@param id string
---@return SessionRecord|nil
function M._for_test_get_active(id)
  return session_records[id]
end

--------------------------------------------------------------------------------
-- Reload support
--------------------------------------------------------------------------------

--- Tear down every *active* (currently-capturing) session: detach its
--- per-session key-tracking callback, disarm any watch trigger, and drop
--- the record. Finished records are untouched. Returns the number of
--- active records destroyed — the reloader uses this to tell the user how
--- many in-flight captures got dropped.
---@return integer dropped
function M.teardown_active()
  local dropped = 0
  local to_destroy = {}
  for id, rec in pairs(session_records) do
    if rec.status == "active" then
      to_destroy[#to_destroy + 1] = id
    end
  end
  for _, id in ipairs(to_destroy) do
    if destroy_record(id) then dropped = dropped + 1 end
  end
  return dropped
end

--- Snapshot the persistent state (finished records + alias/recall indexes)
--- for carry-over across a plugin reload. After `teardown_active` has run,
--- everything left in `session_records` is a finished record with no live
--- nsid/timer references, so a plain shallow copy is safe.
---@return { records: table, manual: table, order: string[], last_finished: string|nil }
function M.dump_for_reload()
  local records_copy = {}
  for id, rec in pairs(session_records) do
    records_copy[id] = rec
  end
  local manual_copy = {}
  for alias, id in pairs(manual_alias_to_id) do
    manual_copy[alias] = id
  end
  local order_copy = {}
  for i, id in ipairs(recall_id_order) do
    order_copy[i] = id
  end
  return {
    records       = records_copy,
    manual        = manual_copy,
    order         = order_copy,
    last_finished = last_finished_id,
  }
end

--- Rehydrate a previous `dump_for_reload()` into this (fresh) module.
--- Called after `package.loaded[...]` has been nil'd and the module has
--- been re-required, so module-level tables start empty.
---@param dump { records: table, manual: table, order: string[], last_finished: string|nil }
function M.restore_from_dump(dump)
  session_records     = dump.records or {}
  manual_alias_to_id  = dump.manual or {}
  recall_id_order     = dump.order or {}
  last_finished_id    = dump.last_finished
end

return M
