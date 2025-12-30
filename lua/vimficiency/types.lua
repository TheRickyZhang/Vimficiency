local M = {}

--- C ABI / Library interface
---@class C_ScoreWeights
---@field w_key number
---@field w_same_finger number
---@field w_same_key number
---@field w_alt_bonus number
---@field w_run_pen number
---@field w_roll_good number
---@field w_roll_bad number

---@class C_KeyInfo
---@field hand integer
---@field finger integer
---@field base_cost number

---@class VimficiencyConfigFFI
---@field default_keyboard integer
---@field weights C_ScoreWeights
---@field keys C_KeyInfo[]

---@class VimficiencyLib
---@field VIMFICIENCY_KEY_COUNT integer
---@field VIMFICIENCY_FINGER_COUNT integer
---@field VIMFICIENCY_HAND_COUNT integer
---@field vimficiency_key_name fun(index: integer): ffi.cdata*
---@field vimficiency_finger_name fun(index: integer): ffi.cdata*
---@field vimficiency_hand_name fun(index: integer): ffi.cdata*
---@field vimficiency_get_config fun(): VimficiencyConfigFFI
---@field vimficiency_apply_config fun(): nil
---@field vimficiency_analyze fun(start_text: string, start_row: integer, start_col: integer, end_text: string, end_row: integer, end_col: integer, keyseq: string): string
---@field vimficiency_get_debug fun(): string
---@field vimficiency_version fun(): integer
---@field vimficiency_debug_config fun(): string


--- Native C++

--- A session is maintained from its initialization from a call to start(), to the time it is consumed by a call to end().
--- It must be over the same window, and will fail if any of its members gets invalidated.
--- Multiple sessions can exist simultaneously to continuously record movements
---@class VimficiencySession
---@field id string
---@field win number
---@field start_buf number
---@field start_state VimficiencyState
---@field key_seq VimficiencyKeyEvent[]
---@field time_started number

---@class VimficiencyKeyEvent
---@field t integer
---@field win integer   --- This will be useful when matching against multiple sessions
---@field buf integer
---@field mode string
---@field key_sent_raw string   ---Probably not needed since not readable, just for debugging
---@field key_sent string
---@field key_typed_raw string --- Typed = representation before any mappings. Just use key everywhere for now.
---@field key_typed string

---@class VimficiencyState
---@field bufname string       # original buffer name (full path)
---@field filetype string      # buffer filetype
---@field row integer
---@field col integer
---@field top_row integer
---@field bottom_row integer
---@field window_height integer # Ctrl-F, B distance
---@field scroll_amount integer # Ctrl-D, U (may be different than window_height/2)
---@field lines string[]       # full buffer contents

---@class VimficiencyWriteDTO
---@field buf number
---@field win number
---@field id string
---@field state VimficiencyState



---@param id string
---@param win number
---@param start_buf number
---@param start_state VimficiencyState
---@return VimficiencySession
function M.new_session(id, win , start_buf , start_state)
  assert(type(id) == "string" and id ~= "", "session.id must be nonempty string")
  assert(vim.api.nvim_win_is_valid(win), "session.win must be a valid window id")
  assert(vim.api.nvim_buf_is_valid(start_buf), "start_buf is not valid" .. start_buf)

  return {
    id = id,
    win = win,
    start_state = start_state,
    start_buf = start_buf,
    key_seq = {},
    time_started = vim.uv.hrtime(),
  }
end


---@param bufname string
---@param filetype string
---@param row integer
---@param col integer
---@param top_row integer
---@param bottom_row integer
---@param window_height integer
---@param scroll_amount integer
---@param lines string[]
---@return VimficiencyState
function M.new_file_contents(
  bufname, filetype, row, col,
  top_row, bottom_row, window_height, scroll_amount,
  lines
)
  assert(type(bufname) == "string", "filecontents.bufname must be string")
  assert(type(filetype) == "string", "filecontents.filetype must be string")
  assert(type(row) == "number" and type(col) == "number", "row and col must be ints")
  assert(type(lines) == "table", "filecontents.lines must be an array of strings")

  return {
    bufname = bufname,
    filetype = filetype,
    row = row,
    col = col,
    top_row = top_row,
    bottom_row = bottom_row,
    window_height = window_height,
    scroll_amount = scroll_amount,
    lines = lines,
  }
end

---@param buf number
---@param win number
---@param id string
---@param state VimficiencyState
---@return VimficiencyWriteDTO
function M.new_write_dto(buf, win, id, state)
  assert(vim.api.nvim_buf_is_valid(buf), "buf is not valid" .. buf)
  assert(vim.api.nvim_win_is_valid(win), "win is not valid" .. win)
  assert(type(id) == "string")
  return {buf=buf, win=win, id=id, state=state}
end

return M
