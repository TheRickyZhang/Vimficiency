local ffi_explore = require("vimficiency.ffi.explore")
local settings = require("vimficiency.explore.settings")

local M = {}
local v = vim.api

---@class VF.Explore.Scratch
---@field buf integer
---@field win integer
---@field tab integer

---@class VF.Explore.Pending
---@field target string
---@field literal_target string
---@field row integer
---@field col_start integer

---View-settings fields are mirrored from the settings store.
---@class VF.Explore.Active : VF.Explore.ViewSettings
---@field label string
---@field result VF.Explore.OpenResult
---@field view_id integer
---@field scratch VF.Explore.Scratch
---@field list_buf integer
---@field state VF.Explore.State
---@field recommendations VF.Explore.Recommendation[]
---@field header_rows VF.Explore.HeaderRows
---@field on_key_buffer string
---@field on_key_events VF.KeyEvent[]
---@field pending VF.Explore.Pending|nil
---@field warning { title: string, lines: string[] }|nil
---@field restoring (true|{context: string})|nil
---@field plan_reconfigure_pending boolean|nil
---@field header_handlers table<string, function>
---@field winclosed_autocmd integer|nil

---@param label string
---@param result VF.Explore.OpenResult
---@param view_id integer
---@param layout VF.Explore.Layout
---@param header_handlers table<string, function>
---@return VF.Explore.Active
function M.create(label, result, view_id, layout, header_handlers)
  local view_settings = settings.settings_store().view
  ---@type VF.Explore.Active
  local view = {
    label = label,
    result = result,
    view_id = view_id,
    scratch = { buf = layout.scratch_buf, win = layout.scratch_win, tab = layout.scratch_tab },
    list_buf = layout.list_buf,
    state = {
      phase = { kind = "Navigate", edit_index = 0 },
      is_completed = false,
      cursor = { row = result.start_row, col = result.start_col },
      total_edits = 0, cost = 0, seq = "",
      can_undo = false, can_redo = false,
      target_range = {
        begin_pos = { row = result.start_row, col = result.start_col },
        end_pos = { row = result.start_row, col = result.start_col },
      },
    },
    recommendations = {},
    header_rows = { explored = {}, optimal = {} },
    on_key_buffer = "",
    on_key_events = {},
    pending = nil,
    warning = nil,
    restoring = nil,
    header_handlers = header_handlers,
    display_mode = view_settings.display_mode,
    recommendation_sort = view_settings.recommendation_sort,
    recommendation_count = view_settings.recommendation_count,
    show_user_typed = view_settings.show_user_typed,
    show_result_count = view_settings.show_result_count,
  }
  return view
end

---@param view VF.Explore.Active
---@return table
function M.status(view)
  local live_cursor = v.nvim_win_get_cursor(view.scratch.win)
  local out = vim.deepcopy(view.state)
  out.scratch_cursor = { row = live_cursor[1] - 1, col = live_cursor[2] }
  out.pending = vim.deepcopy(view.pending)
  out.warning = vim.deepcopy(view.warning)
  out.session_lines = ffi_explore.explore_current_lines(view.view_id)
  out.scratch_lines = v.nvim_buf_get_lines(view.scratch.buf, 0, -1, false)
  out.recommendations = vim.deepcopy(view.recommendations)
  return out
end

return M
