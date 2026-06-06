-- VF.Session.Result display adapters. Stable report text is formatted in C++.

local ffi_display = require("vimficiency.ffi.display")

local M = {}

--- Shown wherever a result is surfaced if mouse/scroll input was stripped from
--- the capture (see `compute.lua` `filter_mouse_events`).
M.MOUSE_WARNING = "⚠ mouse/scroll input ignored — vimfy optimizes keyboard motion"

---@param result VF.Session.Result
---@return string  e.g. "(1,2) → (3,4)"
function M.format_position(result)
  return ffi_display.format_result_position(result)
end

---@param result VF.Session.Result
---@return string  e.g. " [manual]", or ""
function M.format_reason_suffix(result)
  return ffi_display.format_result_reason_suffix(result)
end

--- Format the result body as display lines.
---@param result VF.Session.Result
---@return string[]
function M.format_body(result)
  return ffi_display.format_result_body(result)
end

---@param title string
---@param result VF.Session.Result
---@return string
function M.format_message(title, result)
  return ffi_display.format_result_message(title, result)
end

---@param name string
---@param result VF.Session.Result
---@return string[]
function M.format_saved_lines(name, result)
  return ffi_display.format_saved_result_lines(name, result)
end

return M
