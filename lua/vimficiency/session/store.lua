-- Canonical session store plus two indexes:
-- `manual_alias_to_id` for manual handles and `recall_id_order` for the
-- rolling recall ring.

local M = {}

local alias_mod = require("vimficiency.session.alias")
local ffi_lib = require("vimficiency.ffi")
local key_tracking = require("vimficiency.capture.key_tracking")
local util = require("vimficiency.util")
local config = require("vimficiency.config")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

--- Canonical session record.
--- Active records accumulate `key_seq`; finished records hold `result`.
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

--- Normalized view-model for list/suggest UIs.
--- `display_alias` is presentation-only; use `id` for follow-up actions.
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

--- Derive the canonical SessionType from the kind pair.
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

  -- Do not silently fall back to a shorter-than-requested window.
  local snapped = resolve_recall_cutoff(target)
  if not snapped then return nil end
  return recall_id_order[snapped]
end

--------------------------------------------------------------------------------
-- Internal mutators
--------------------------------------------------------------------------------

--- Detach key tracking for a record and remove it from `session_records`.
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

--- Splice an id out of `recall_id_order`, if present.
---@param id string
local function unindex_recall(id)
  for i = 1, #recall_id_order do
    if recall_id_order[i] == id then
      table.remove(recall_id_order, i)
      return
    end
  end
end

--- Evict oldest recall ids only when both retention caps say so.
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
--- The cap only counts active sessions. Finished records keep their alias
--- slot but do not block a new start.
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

--- Id-keyed variant of `get_result`.
--- Use this after pinning a time-varying alias to a stable id.
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

--- Remove a session entirely by stable id.
---@param id string
function M.remove(id)
  -- Removing by alias is unsafe because recall aliases drift.
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

--- Transition an active session to finished without removing its indexes.
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
---@return ResultSession|nil
function M.get_last_finished_result()
  if not last_finished_id then return nil end
  local rec = session_records[last_finished_id]
  return rec and rec.result or nil
end

--- Alias passed when the most recent session finished.
---@return string|nil
function M.get_last_finished_alias()
  if not last_finished_id then return nil end
  local rec = session_records[last_finished_id]
  return rec and rec.finish_alias or nil
end

--- Resolve an alias to its session id.
---@param alias string
---@return string|nil id
function M.get_id(alias)
  return convert_alias_to_id(alias)
end

--- Id of the most recently finished session.
---@return string|nil id
function M.get_last_finished_id()
  if not last_finished_id then return nil end
  if not session_records[last_finished_id] then return nil end
  return last_finished_id
end

--- Insert a disk-loaded result as a finished session under `alias`.
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

  -- `util.new_id()` needs a valid buffer; the id itself is opaque.
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

--- Move a manual alias to a new name in-place. Refuses if the source is
--- missing or the target is already taken. Only the `manual_alias_to_id`
--- index is touched; the underlying SessionRecord is untouched.
---@param old_alias string
---@param new_alias string
---@return boolean ok
---@return string|nil err
function M.rename_manual_alias(old_alias, new_alias)
  if old_alias == new_alias then
    return false, "source and target aliases are identical"
  end
  if not alias_mod.is_valid_manual(new_alias) then
    return false, "invalid target alias '" .. tostring(new_alias) ..
      "' (manual aliases must be alphabetic)"
  end
  local id = manual_alias_to_id[old_alias]
  if not id then
    return false, "no manual alias '" .. tostring(old_alias) .. "' in the workspace"
  end
  if manual_alias_to_id[new_alias] then
    return false, "alias '" .. new_alias .. "' is already in use"
  end
  manual_alias_to_id[old_alias] = nil
  manual_alias_to_id[new_alias] = id
  local rec = session_records[id]
  if rec then rec.finish_alias = new_alias end
  return true, nil
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

--- Build a SessionSummary for the session with this id.
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
    -- Recall aliases drift; this is presentation-only.
    display_alias = tostring(#recall_id_order - recall_index + 1)
  end

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

--- Summarize every known session. Order is unspecified.
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

--- Ingest one tracked keystroke into the rolling recall ring.
---
--- Callers are responsible for event-source filtering and window validity.
---@param event VimficiencyKeyEvent
function M.ingest_recall_event(event)
  -- Append the event to every still-active recall record.
  for _, id in ipairs(recall_id_order) do
    local rec = session_records[id]
    if rec and rec.status == "active" then
      table.insert(rec.key_seq, event)
      rec.key_count = rec.key_count + 1
    end
  end

  -- Create a new record from the pre-key state.
  local buf = event.buf
  local win = event.win
  local id = util.new_id(buf)
  local start_state = util.capture_state(buf, win)

  -- Recall is auto-start/manual-end until auto_suggest takes over.
  local rec = M.new_active_session(id, -1, win, buf, start_state, "auto", "manual")

  -- Include the current key because the state was captured before it ran.
  table.insert(rec.key_seq, event)
  rec.key_count = 1
  rec.first_mode = event.mode

  M.store_recall(rec)
end

--- Strip pre-resolution mapping bytes from every active recall record.
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

-- Test-only pure helpers.
M._pure = {
  resolve_recall_cutoff = resolve_recall_cutoff_pure,
  snap_backward_to_boundary = snap_backward_to_boundary_pure,
}

--- Test-only: fetch an active record by id.
---@param id string
---@return SessionRecord|nil
function M._for_test_get_active(id)
  return session_records[id]
end

--------------------------------------------------------------------------------
-- Reload support
--------------------------------------------------------------------------------

--- Tear down every active session and return the number dropped.
--- Routes through `M.remove` rather than calling `destroy_record` directly,
--- so `manual_alias_to_id` and `recall_id_order` get un-indexed alongside
--- the record deletion. Calling `destroy_record` alone leaves dangling
--- alias/ring entries that would survive `dump_for_reload` → `restore_from_dump`
--- and resurrect phantom sessions (get_id returns ids with no backing record,
--- list() shows aliases that resolve to nothing).
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
    if session_records[id] ~= nil then
      M.remove(id)
      dropped = dropped + 1
    end
  end
  return dropped
end

--- Snapshot finished records and indexes for plugin reload.
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

--- Rehydrate a previous `dump_for_reload()` into this fresh module.
--- Filters the manual and recall indexes against `records` as it goes —
--- any id that doesn't have a backing record gets dropped. With the
--- current `teardown_active` routing through `M.remove`, dumps should
--- already be clean, but this guard keeps the invariant explicit: after
--- restore, every entry in `manual_alias_to_id` and `recall_id_order`
--- resolves to a real record.
---@param dump { records: table, manual: table, order: string[], last_finished: string|nil }
function M.restore_from_dump(dump)
  session_records = dump.records or {}

  manual_alias_to_id = {}
  for alias, id in pairs(dump.manual or {}) do
    if session_records[id] then
      manual_alias_to_id[alias] = id
    end
  end

  recall_id_order = {}
  for _, id in ipairs(dump.order or {}) do
    if session_records[id] then
      recall_id_order[#recall_id_order + 1] = id
    end
  end

  last_finished_id =
    (dump.last_finished and session_records[dump.last_finished])
      and dump.last_finished
      or nil
end

return M
