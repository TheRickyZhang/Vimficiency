-- lua/vimficiency/alias.lua
-- Single source of truth for Vimfy alias grammar.
--
-- Three valid kinds of alias:
--   manual       ^%a+$       live session handle (alphabetic only)
--   recall_key   ^%d+$       "N keystrokes ago" (recall ring)
--   recall_time  ^%d+s$      "N seconds ago" (recall ring, boundary-snapped)
--
-- Anything else is invalid. Callers that used to accept "not recall → manual"
-- (pre-refactor) should now reject unknown forms explicitly; see
-- `session.start`.
--
-- This module is pure Lua (no `vim.*`) so its tests can run under plain
-- LuaJIT and the grammar stays the only thing under test.

local M = {}

---@alias AliasType "manual" | "recall_key" | "recall_time"

--- Common `Ns` windows surfaced by tab completion. Any `Ns` is valid at
--- runtime; these are here so the feature is discoverable.
M.TIME_HINTS = { "3s", "5s", "10s", "30s" }

--- Parse an alias. Returns (type, value) for valid aliases, or (nil, nil)
--- for anything that doesn't match the three forms. `value` is the numeric
--- payload for recall aliases (keys or seconds); nil for manual.
---@param s any
---@return AliasType|nil type
---@return integer|nil value
function M.parse(s)
  if type(s) ~= "string" or s == "" then return nil, nil end

  local secs = s:match("^(%d+)s$")
  if secs then
    local n = tonumber(secs)
    if n and n > 0 then return "recall_time", n end
    return nil, nil
  end

  local keys = s:match("^(%d+)$")
  if keys then
    local n = tonumber(keys)
    if n and n > 0 then return "recall_key", n end
    return nil, nil
  end

  if s:match("^%a+$") then
    return "manual", nil
  end

  return nil, nil
end

--- True iff `s` is a valid manual alias.
---@param s any
---@return boolean
function M.is_valid_manual(s)
  return type(s) == "string" and s:match("^%a+$") ~= nil
end

--- True iff `s` is any valid recall alias (key or time form).
---@param s any
---@return boolean
function M.is_recall(s)
  local t = M.parse(s)
  return t == "recall_key" or t == "recall_time"
end

return M
