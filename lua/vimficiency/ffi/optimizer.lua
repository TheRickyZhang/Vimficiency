local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local DEBUG_MARKER = "----------------DEBUG----------------"
local SEP = core.EVENT_FIELD_SEP

-- Decoder for `payload::encodeDiffRegions` (LuaExports/Common.h).
---@param line string
---@return VF.Diff.Region
local function parse_diff_region(line)
  local f = vim.split(line, SEP, { plain = true })
  if #f ~= 8 then error("malformed diff region line: " .. line) end
  return {
    init = { begin_row = tonumber(f[1]), begin_col = tonumber(f[2]),
             end_row = tonumber(f[3]), end_col = tonumber(f[4]) },
    goal = { begin_row = tonumber(f[5]), begin_col = tonumber(f[6]),
             end_row = tonumber(f[7]), end_col = tonumber(f[8]) },
  }
end

---@param result_str string
---@return VF.Diff.Region[]
local function parse_diff_regions(result_str)
  local regions = {}
  for line in result_str:gmatch("[^\n]+") do
    regions[#regions + 1] = parse_diff_region(line)
  end
  return regions
end

M._parse_diff_regions = parse_diff_regions

-- Count-framed: `size: N user_cost: X`, N result lines, `diffs: M`, M region
-- lines, then an optional DEBUG trailer that is only looked for between
-- sections (dev/lua/ffi-separators.md § Convention 3).
---@param result_str string
---@return VF.Optimizer.Result[] results, number user_cost, VF.Diff.Region[] diffs
local function parse_analyze_results(result_str)
  local results, diffs = {}, {}
  local user_cost = 0
  local header_seen = false
  local pending_results, pending_diffs = 0, 0
  for line in result_str:gmatch("[^\n]+") do
    if not header_seen then
      header_seen = true
      pending_results = tonumber(line:match("size:%s*(%d+)")) or 0
      user_cost = tonumber(line:match("user_cost:%s*(%S+)")) or 0
    elseif pending_results > 0 then
      pending_results = pending_results - 1
      local seq, cost_str = line:match("^(.*)\x1F(%S+)$")
      if not seq then error("malformed analyze result line: " .. line) end
      results[#results + 1] = { seq = seq, cost = tonumber(cost_str) or 0 }
    elseif pending_diffs > 0 then
      pending_diffs = pending_diffs - 1
      diffs[#diffs + 1] = parse_diff_region(line)
    elseif line:find(DEBUG_MARKER, 1, true) then
      break
    else
      pending_diffs = tonumber(line:match("^diffs:%s*(%d+)")) or 0
    end
  end
  return results, user_cost, diffs
end

M._parse_analyze_results = parse_analyze_results

-- The async worker runs the search (and thus debug accumulation) on its own
-- thread, so its debug output is embedded in the result string rather than
-- reachable via the main-thread `vf_get_debug()`. Pull it back out at poll time.
---@param result_str string
---@return string
local function extract_debug(result_str)
  local idx = result_str:find(DEBUG_MARKER, 1, true)
  if not idx then return "" end
  local nl = result_str:find("\n", idx, true)
  if not nl then return "" end
  return result_str:sub(nl + 1)
end

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
---@return VF.Optimizer.Result[] results, number user_cost, string debug, VF.Diff.Region[] diffs
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

  local results, user_cost, diffs = parse_analyze_results(result_str)
  return results, user_cost, dbg, diffs
end

---@class VF.Optimizer.AnalyzeResult
---@field results VF.Optimizer.Result[]
---@field user_cost number
---@field dbg string
---@field diffs VF.Diff.Region[]   # The planner's regions the search ran against; empty for pure navigation

--- Kicks off `analyze` on a C++ worker thread and returns a job id immediately.
--- Same arguments as `M.analyze` plus `deadline_ms` (0 = run to natural caps).
--- Poll with `analyze_poll` until it stops returning nil.
---@param deadline_ms integer|nil
---@return integer job_id
function M.analyze_start_async(
  initial_lines, goal_lines,
  boundary_first_col, boundary_last_col,
  has_lines_above, has_lines_below,
  start_row, start_col, end_row, end_col,
  key_seq,
  window_height, scroll_amount,
  RESULTS_CALCULATED,
  optimizer_overrides,
  deadline_ms
)
  local initial_payload = core.encode_line_array(initial_lines, "initial_lines")
  local goal_payload = core.encode_line_array(goal_lines, "goal_lines")
  local user_seq = key_seq or ""
  local override_payload = optimizer_overrides or ""

  local payload = core.slice_to_string(lib.vf_analyze_start_async(
    initial_payload, #initial_payload,
    goal_payload, #goal_payload,
    boundary_first_col, boundary_last_col,
    has_lines_above, has_lines_below,
    start_row, start_col, end_row, end_col,
    user_seq, #user_seq,
    window_height, scroll_amount,
    RESULTS_CALCULATED,
    override_payload, #override_payload,
    deadline_ms or 0
  ))
  if payload:sub(1, 6) == "ERROR:" then
    error(payload)
  end
  return tonumber(payload)
    or error("analyze_start_async returned non-numeric job_id: " .. payload)
end

--- Returns the parsed analyze result once the worker finishes, or nil while it
--- is still computing.
---@param job_id integer
---@return VF.Optimizer.AnalyzeResult|nil
function M.analyze_poll(job_id)
  local result_str = core.slice_to_string(lib.vf_analyze_poll(job_id))
  if result_str == "pending" then return nil end
  if result_str:sub(1, 6) == "ERROR:" then
    error(result_str)
  end
  local results, user_cost, diffs = parse_analyze_results(result_str)
  return {
    results = results,
    user_cost = user_cost,
    dbg = extract_debug(result_str),
    diffs = diffs,
  }
end

---@param job_id integer
---@return boolean
function M.analyze_cancel(job_id)
  return lib.vf_analyze_cancel(job_id) == 1
end

--- Standalone planner run for a result that lacks stored diffs (files saved
--- before the analyze payload carried them). Live sessions get their regions
--- from `analyze`/`analyze_poll` instead — those are the partition the search
--- actually ran against, whereas this uses default planner cost options.
---@param initial_lines string[]
---@param goal_lines string[]
---@param diff_algorithm integer|nil
---@return VF.Diff.Region[]
function M.compute_diffs(initial_lines, goal_lines, diff_algorithm)
  if diff_algorithm == nil then
    diff_algorithm = M.get_optimizer_defaults().composition.diffAlgorithm or 0
  end
  local initial_payload = core.encode_line_array(initial_lines, "initial_lines")
  local goal_payload = core.encode_line_array(goal_lines, "goal_lines")
  local result_str = core.slice_to_string(lib.vf_compute_diffs(
    initial_payload, #initial_payload, goal_payload, #goal_payload, diff_algorithm))
  if result_str:sub(1, 6) == "ERROR:" then
    error(result_str)
  end
  return parse_diff_regions(result_str)
end

return M
