local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local function drain_warnings()
  local pending = core.slice_to_string(lib.vf_get_warnings())
  if pending == "" then return end
  pcall(vim.notify, "[vimficiency] " .. pending, vim.log.levels.WARN)
end

---@param payload string
---@return string
local function require_non_error(payload)
  drain_warnings()
  if payload:sub(1, 7) == "ERROR: " then
    error(payload, 0)
  end
  return payload
end

---@param kind string
---@param edit_index_str string
---@return VF.Explore.Phase
local function build_phase(kind, edit_index_str)
  local edit_index = tonumber(edit_index_str) or 0
  if kind == "Navigate" or kind == "Transform" or kind == "Insert" then
    return { kind = kind, edit_index = edit_index }
  end
  error("unknown phase kind from FFI: " .. tostring(kind))
end

---@param payload string
---@return VF.Explore.State
local function parse_explore_state(payload)
  local parts = core.decode_string_list(payload)
  assert(#parts == 14, "explore state payload must have 14 fields")
  local target_range = {
    begin_pos = { row = tonumber(parts[11]) or 0, col = tonumber(parts[12]) or 0 },
    end_pos = { row = tonumber(parts[13]) or 0, col = tonumber(parts[14]) or 0 },
  }
  return {
    phase = build_phase(parts[1], parts[2]),
    is_completed = parts[3] == "1",
    cursor = { row = tonumber(parts[4]) or 0, col = tonumber(parts[5]) or 0 },
    total_edits = tonumber(parts[6]) or 0,
    cost = tonumber(parts[7]) or 0,
    seq = parts[8],
    can_undo = parts[9] == "1",
    can_redo = parts[10] == "1",
    target_range = target_range,
  }
end

---@param payload string
---@return VF.Explore.ApplyResult
local function parse_explore_apply_result(payload)
  local parts = core.decode_string_list(payload)
  assert(#parts == 2, "explore apply payload must have 2 fields")
  local status = parts[1]
  if status == "Applied" then
    return { status = "Applied" }
  elseif status == "Rejected" then
    return { status = "Rejected", reason = parts[2] }
  end
  error("unknown apply-result status from FFI: " .. tostring(status))
end

---@param payload string
---@return VF.Explore.Recommendation[]
local function parse_explore_recommendations(payload)
  local parts = core.decode_string_list(payload)
  assert(#parts >= 1, "explore recommendations payload must have count prefix")
  local count = tonumber(parts[1]) or 0
  local expected = 1 + count * 7
  assert(#parts == expected,
    "explore recommendations payload has " .. #parts ..
    " fields, expected " .. expected .. " for count=" .. count)
  local recs = {}
  for i = 1, count do
    local base = 1 + (i - 1) * 7
    recs[i] = {
      text = parts[base + 1],
      cost_diff = tonumber(parts[base + 2]) or 0,
      distance = tonumber(parts[base + 3]) or 0,
      score = tonumber(parts[base + 4]) or 0,
      landing = {
        row = tonumber(parts[base + 5]) or 0,
        col = tonumber(parts[base + 6]) or 0,
      },
      literal_text = parts[base + 7],
    }
  end
  return recs
end

function M.explore_start(initial_lines, start_row, start_col,
                         goal_lines, end_row, end_col,
                         boundary_first_col, boundary_last_col,
                         has_lines_above, has_lines_below,
                         window_height, scroll_amount,
                         user_seq, optimizer_overrides)
  local initial_payload = core.encode_line_array(initial_lines, "initial_lines")
  local goal_payload = core.encode_line_array(goal_lines, "goal_lines")
  local user_payload = user_seq or ""
  local override_payload = optimizer_overrides or ""
  local payload = require_non_error(core.slice_to_string(lib.vf_explore_start(
    initial_payload,
    #initial_payload,
    start_row, start_col,
    goal_payload,
    #goal_payload,
    end_row, end_col,
    boundary_first_col, boundary_last_col,
    has_lines_above, has_lines_below,
    window_height, scroll_amount,
    user_payload,
    #user_payload,
    override_payload,
    #override_payload
  )))
  return tonumber(payload) or error("explore_start returned non-numeric view_id: " .. payload)
end

function M.explore_destroy(view_id)
  return lib.vf_explore_destroy(view_id) == 1
end

function M.explore_state(view_id)
  local payload = require_non_error(core.slice_to_string(lib.vf_explore_state(view_id)))
  return parse_explore_state(payload)
end

function M.explore_recommendations(view_id, max_count, optimizer_overrides, sort_mode)
  local override_payload = optimizer_overrides or ""
  local sort_payload = sort_mode or "effort"
  local payload = require_non_error(core.slice_to_string(
    lib.vf_explore_recommendations(
      view_id, max_count,
      override_payload, #override_payload,
      sort_payload, #sort_payload)))
  return parse_explore_recommendations(payload)
end

function M.explore_reconfigure_plan(view_id, optimizer_overrides)
  local override_payload = optimizer_overrides or ""
  local payload = require_non_error(core.slice_to_string(
    lib.vf_explore_reconfigure_plan(view_id, override_payload, #override_payload)))
  return { reset = payload == "1" }
end

function M.explore_apply_movement(view_id, movement_text)
  local payload = require_non_error(core.slice_to_string(
    lib.vf_explore_apply_movement(view_id, movement_text, #movement_text)))
  return parse_explore_apply_result(payload)
end

function M.explore_apply_edit(view_id, text)
  local payload = require_non_error(core.slice_to_string(
    lib.vf_explore_apply_edit(view_id, text, #text)))
  return parse_explore_apply_result(payload)
end

function M.explore_current_lines(view_id)
  local payload = require_non_error(core.slice_to_string(lib.vf_explore_current_lines(view_id)))
  return core.decode_string_list(payload)
end

function M.explore_accept_snapshot(view_id, lines, new_row, new_col, raw_keys, insert_mode)
  local lines_payload = core.encode_line_array(lines, "lines")
  local key_payload = raw_keys or ""
  local payload = require_non_error(core.slice_to_string(lib.vf_explore_accept_snapshot(
    view_id, lines_payload, #lines_payload, new_row, new_col,
    key_payload, #key_payload, insert_mode)))
  return parse_explore_apply_result(payload)
end

function M.explore_undo(view_id)
  local payload = require_non_error(core.slice_to_string(lib.vf_explore_undo(view_id)))
  return parse_explore_apply_result(payload)
end

function M.explore_redo(view_id)
  local payload = require_non_error(core.slice_to_string(lib.vf_explore_redo(view_id)))
  return parse_explore_apply_result(payload)
end

function M.explore_header_rows(view_id)
  local payload = require_non_error(core.slice_to_string(lib.vf_explore_header_rows(view_id)))
  local parts = core.decode_string_list(payload)
  local idx = 1
  local explored_count = tonumber(parts[idx]) or 0
  idx = idx + 1
  local explored = {}
  for i = 1, explored_count do
    explored[i] = parts[idx + i - 1]
  end
  idx = idx + explored_count
  local optimal_count = tonumber(parts[idx]) or 0
  idx = idx + 1
  local optimal = {}
  for i = 1, optimal_count do
    local col_count = tonumber(parts[idx]) or 0
    idx = idx + 1
    local col = {}
    for j = 1, col_count do
      col[j] = parts[idx + j - 1]
    end
    idx = idx + col_count
    optimal[i] = col
  end
  return { explored = explored, optimal = optimal }
end

return M
