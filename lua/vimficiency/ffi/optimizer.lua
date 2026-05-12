local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

---@param result_str string
---@return VF.Optimizer.Result[] results, number user_cost
local function parse_analyze_results(result_str)
  local results = {}
  local user_cost = 0
  local line_num = 0
  for line in result_str:gmatch("[^\n]+") do
    if line:find("----------------DEBUG----------------", 1, true) then
      break
    end
    line_num = line_num + 1
    if line_num == 1 then
      local cost_str = line:match("user_cost:%s*(%S+)")
      if cost_str then
        user_cost = tonumber(cost_str) or 0
      end
    else
      local seq, cost_str = line:match("^(.*)\x1F(%S+)$")
      if seq then
        table.insert(results, {
          seq = seq,
          cost = tonumber(cost_str) or 0,
        })
      end
    end
  end
  return results, user_cost
end

M._parse_analyze_results = parse_analyze_results

---@param overrides? VF.OptimizerOverrides
---@return string
function M.encode_optimizer_overrides(overrides)
  if not overrides then return "" end
  local lines = {}
  for _, scope in ipairs({ "shared", "nav", "transform", "composition" }) do
    local kvs = overrides[scope]
    if kvs then
      for k, v in pairs(kvs) do
        local encoded
        if type(v) == "boolean" then
          encoded = v and "1" or "0"
        else
          encoded = tostring(v)
        end
        lines[#lines + 1] = scope .. ":" .. k .. "=" .. encoded
      end
    end
  end
  return table.concat(lines, "\n")
end

local optimizer_defaults_cache = nil

function M.get_optimizer_defaults()
  if optimizer_defaults_cache ~= nil then return optimizer_defaults_cache end
  local raw = core.slice_to_string(lib.vf_get_optimizer_defaults())
  local out = { shared = {}, nav = {}, transform = {}, composition = {} }
  for line in string.gmatch(raw, "[^\n]+") do
    local scope, key, typ, value = line:match("^([^:]+):([^:]+):([^=]+)=(.*)$")
    if scope and out[scope] then
      if typ == "int" then
        out[scope][key] = tonumber(value)
      elseif typ == "double" then
        out[scope][key] = tonumber(value)
      elseif typ == "bool" then
        out[scope][key] = (value == "1" or value == "true")
      else
        out[scope][key] = value
      end
    end
  end
  optimizer_defaults_cache = out
  return out
end

---@param initial_lines string[]
---@param goal_lines string[]
---@param boundary_first_col integer
---@param boundary_last_col integer
---@param has_lines_above boolean
---@param has_lines_below boolean
---@param start_row integer
---@param start_col integer
---@param end_row integer
---@param end_col integer
---@param key_seq string
---@param window_height integer
---@param scroll_amount integer
---@param RESULTS_CALCULATED integer
---@param optimizer_overrides? string
---@return VF.Optimizer.Result[] results, number user_cost, string debug
function M.analyze(
  initial_lines, goal_lines,
  boundary_first_col, boundary_last_col,
  has_lines_above, has_lines_below,
  start_row, start_col, end_row, end_col,
  key_seq,
  window_height, scroll_amount,
  RESULTS_CALCULATED,
  optimizer_overrides
)
  local initial_payload = core.encode_line_array(initial_lines, "initial_lines")
  local goal_payload = core.encode_line_array(goal_lines, "goal_lines")
  local user_seq = key_seq or ""
  local override_payload = optimizer_overrides or ""

  local result = lib.vf_analyze(
    initial_payload, #initial_payload,
    goal_payload, #goal_payload,
    boundary_first_col, boundary_last_col,
    has_lines_above, has_lines_below,
    start_row, start_col, end_row, end_col,
    user_seq, #user_seq,
    window_height, scroll_amount,
    RESULTS_CALCULATED,
    override_payload, #override_payload
  )
  local dbg = core.slice_to_string(lib.vf_get_debug())
  local result_str = core.slice_to_string(result)

  if result_str:sub(1, 6) == "ERROR:" then
    error(result_str)
  end

  local results, user_cost = parse_analyze_results(result_str)
  return results, user_cost, dbg
end

return M
