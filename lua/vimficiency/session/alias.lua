-- Single source of truth for Vimfy alias grammar.
-- Valid forms: manual (`^%a+$`), recall_key (`^%d+$`), recall_time (`^%d+s$`).

local M = {}

---@alias VF.Session.AliasType "manual" | "recall_key" | "recall_time"

--- Common `Ns` windows surfaced by tab completion.
M.TIME_HINTS = { "3s", "5s", "10s", "30s" }

--- Parse an alias.
--- Returns `(type, value)` for valid aliases, else `(nil, nil)`.
---@param s any
---@return VF.Session.AliasType|nil type
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

--- True iff `s` is a safe saved-result name.
--- Grammar: `^[%w_][%w._-]*$`.
---@param s any
---@return boolean
function M.is_valid_saved_name(s)
  return type(s) == "string" and s:match("^[%w_][%w._-]*$") ~= nil
end

return M
