-- lua/vimficiency/session_store.lua
-- Manages sessions across three storage mechanisms with separate active/result tables.
--
-- Storage types:
-- 1. Manual:     map<alias, id> - user-controlled, no auto-eviction
-- 2. Time-based: deque of ids - auto-ends after idle (TODO)
--                alias "." = last, ".." = second to last, etc.
-- 3. Key-count:  deque of ids - rolling FIFO per keystroke
--                alias "1" = last (1 ago), "2" = second to last (2 ago), etc.

local M = {}

local key_tracking = require("vimficiency.key_tracking")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

--- ActiveSession: data for a session that has been started but not finished.
--- Stored while session is in progress; discarded after finish().
---@class ActiveSession
---@field id string                    # Unique identifier (for debug/logging)
---@field key_nsid integer             # Key tracking namespace ID (for cleanup)
---@field win integer                  # Window where session started
---@field buf integer                  # Buffer where session started
---@field start_state VimficiencyState # Cursor/viewport state at start
---@field key_seq VimficiencyKeyEvent[] # Accumulated keypresses
---@field time_started integer         # hrtime when started

--- ResultSession: data for a completed session, ready for simulate().
--- Created at finish(); this is what gets stored long-term.
---@class ResultSession
---@field lines string[]               # Trimmed buffer lines used for optimization
---@field start_row integer            # 0-indexed, relative to lines
---@field start_col integer            # 0-indexed
---@field end_row integer              # 0-indexed, relative to lines
---@field end_col integer              # 0-indexed
---@field user_seq string              # What the user typed
---@field optimal_seqs string[]        # Top N results from optimizer
---@field timestamp integer            # hrtime when finished

--------------------------------------------------------------------------------
-- ActiveSession Constructor
--------------------------------------------------------------------------------

---@param id string
---@param key_nsid integer
---@param win integer
---@param buf integer
---@param start_state VimficiencyState
---@return ActiveSession
function M.new_active_session(id, key_nsid, win, buf, start_state)
  assert(type(id) == "string" and id ~= "", "session.id must be nonempty string")
  assert(type(key_nsid) == "number", "key_nsid must be a number")
  assert(vim.api.nvim_win_is_valid(win), "win must be a valid window id")
  assert(vim.api.nvim_buf_is_valid(buf), "buf is not valid: " .. buf)

  return {
    id = id,
    key_nsid = key_nsid,
    win = win,
    buf = buf,
    start_state = start_state,
    key_seq = {},
    time_started = vim.uv.hrtime(),
  }
end

--------------------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------------------

local MANUAL_CAPACITY = 5      -- aliases: a-e
local TIME_CAPACITY = 5        -- aliases: ., .., ...
local KEY_COUNT_CAPACITY = 10  -- aliases: 1-10

--------------------------------------------------------------------------------
-- Storage tables
--------------------------------------------------------------------------------

---@type table<string, ActiveSession>
local active_sessions = {}

---@type table<string, ResultSession>
local result_sessions = {}

---@type table<string, string>  -- alias -> id
local manual_alias_to_id = {}
local manual_count = 0

---@type string[]  -- ordered deque of ids (oldest first, newest last)
local time_id_order = {}

---@type string[]  -- ordered deque of ids (oldest first, newest last)
local key_id_order = {}

--------------------------------------------------------------------------------
-- Local helpers
--------------------------------------------------------------------------------

---@alias SessionType "manual" | "time" | "key"

---@param alias string
---@return SessionType
local function get_alias_type(alias)
  if alias:match("^%.+$") then
    return "time"
  end
  if alias:match("^%d+$") then
    return "key"
  end
  return "manual"
end

--- Convert alias to session ID
--- Manual: lookup in map
--- Time: "." = last, ".." = second to last (index from end = #dots)
--- Key: "1" = last, "3" = third from end (index from end = number)
---@param alias string
---@return string|nil id
local function convert_alias_to_id(alias)
  local session_type = get_alias_type(alias)

  if session_type == "manual" then
    return manual_alias_to_id[alias]

  elseif session_type == "time" then
    local dot_count = #alias
    local index = #time_id_order - dot_count + 1
    if index >= 1 and index <= #time_id_order then
      return time_id_order[index]
    end
    return nil

  else -- key
    local num = tonumber(alias)
    if not num or num < 1 then return nil end
    local index = #key_id_order - num + 1
    if index >= 1 and index <= #key_id_order then
      return key_id_order[index]
    end
    return nil
  end
end

---@param id string
---@return boolean
local function remove_active(id)
  local active = active_sessions[id]
  if not active then
    return false
  end
  if active and active.key_nsid then
    key_tracking.detach(active.key_nsid)
  end
  active_sessions[id] = nil
  return true
end

---@param id string
local function remove_result(id)
  result_sessions[id] = nil
end

--------------------------------------------------------------------------------
-- Public API
--------------------------------------------------------------------------------

-- Expose get_alias_type for external validation (e.g., session.lua)
M.get_alias_type = get_alias_type

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
---@param active ActiveSession
---@return boolean overwrote  True if an existing session was replaced
function M.store_manual(alias, active)
  local existing_id = manual_alias_to_id[alias]

  if existing_id then
    remove_active(existing_id)
    remove_result(existing_id)
  else
    manual_count = manual_count + 1
  end

  manual_alias_to_id[alias] = active.id
  active_sessions[active.id] = active
  return existing_id ~= nil
end

--- Store a time-based session (stub - creation is automatic)
---@param active ActiveSession
---@return boolean success, string|nil error
function M.store_time(active)
  -- TODO: implement time-based session logic
  return false, "Time-based sessions not yet implemented"
end

--- Store a key-count session, evicting oldest if at capacity
---@param active ActiveSession
---@return boolean success, string|nil error
function M.store_key(active)
  while #key_id_order >= KEY_COUNT_CAPACITY do
    local oldest_id = table.remove(key_id_order, 1)
    if oldest_id then
      remove_active(oldest_id)
      remove_result(oldest_id)
    end
  end

  active_sessions[active.id] = active
  table.insert(key_id_order, active.id)

  return true, nil
end

---@return ActiveSession|nil
function M.get_active(alias)
  local id = convert_alias_to_id(alias)
  if not id then return nil end
  return active_sessions[id]
end

---@param alias string
---@return ResultSession|nil
function M.get_result(alias)
  local id = convert_alias_to_id(alias)
  if not id then return nil end
  return result_sessions[id]
end

---@param alias string
---@return boolean
function M.has_active(alias)
  local id = convert_alias_to_id(alias)
  return id ~= nil and active_sessions[id] ~= nil
end

---@param alias string
---@return boolean
function M.has_result(alias)
  local id = convert_alias_to_id(alias)
  return id ~= nil and result_sessions[id] ~= nil
end

--- Remove a session (both active and result) by alias
---@param alias string
function M.remove(alias)
  local session_type = get_alias_type(alias)

  if session_type == "manual" then
    local id = manual_alias_to_id[alias]
    if not id then return end
    remove_active(id)
    remove_result(id)
    manual_alias_to_id[alias] = nil
    manual_count = manual_count - 1

  elseif session_type == "time" then
    local dot_count = #alias
    local index = #time_id_order - dot_count + 1
    if index < 1 or index > #time_id_order then return end
    local id = table.remove(time_id_order, index)
    if id then
      remove_active(id)
      remove_result(id)
    end

  else -- key
    local num = tonumber(alias)
    if not num or num < 1 then return end
    local index = #key_id_order - num + 1
    if index < 1 or index > #key_id_order then return end
    local id = table.remove(key_id_order, index)
    if id then
      remove_active(id)
      remove_result(id)
    end
  end
end

--- Transition session from active to result
---@param alias string
---@param result ResultSession
---@return boolean success
function M.finish_session(alias, result)
  local id = convert_alias_to_id(alias)
  if not id then
    return false
  end

  if not remove_active(id) then
    return false
  end

  result_sessions[id] = result

  return true
end

--- List all valid aliases for debugging
---@return string[]
function M.list_aliases()
  local aliases = {}

  for alias, _ in pairs(manual_alias_to_id) do
    table.insert(aliases, alias)
  end

  for i = 1, #time_id_order do
    table.insert(aliases, string.rep(".", i))
  end

  for i = 1, #key_id_order do
    table.insert(aliases, tostring(i))
  end

  return aliases
end

return M
