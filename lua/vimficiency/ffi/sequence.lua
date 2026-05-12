local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local KIND_CHAR_TO_NAME = {
  M = "movement",
  D = "delete",
  C = "change",
  V = "visual",
  T = "typed",
  E = "escape",
}

---@param result_str string
---@return VF.Sequence.Token[]
local function parse_kinded_tokens(result_str)
  local out = {}
  for line in result_str:gmatch("([^\n]+)") do
    local kind = KIND_CHAR_TO_NAME[line:sub(1, 1)]
    local text = line:sub(3)
    out[#out + 1] = { text = text, kind = kind or "movement" }
  end
  return out
end

---@param seq string
---@return VF.Sequence.Token[] tokens
---@return string|nil error
function M.tokenize_movements(seq)
  if not seq or seq == "" then return {}, nil end
  local result_str = core.slice_to_string(lib.vf_tokenize_movements(seq, #seq))
  if result_str == "" then return {}, nil end
  if result_str:sub(1, 6) == "ERROR:" then return {}, result_str end
  return parse_kinded_tokens(result_str), nil
end

---@param seq string
---@return VF.Sequence.Token[] tokens
---@return string|nil error
function M.tokenize_sequence(seq)
  if not seq or seq == "" then return {}, nil end
  local result_str = core.slice_to_string(lib.vf_tokenize_sequence(seq, #seq))
  if result_str == "" then return {}, nil end
  if result_str:sub(1, 6) == "ERROR:" then return {}, result_str end
  return parse_kinded_tokens(result_str), nil
end

---@param seq string
---@return string
function M.format_sequence(seq)
  if not seq or seq == "" then
    return ""
  end
  return core.slice_to_string(lib.vf_format_sequence(seq, #seq))
end

---@param lines string[]
---@param start_row integer
---@param start_col integer
---@param seq string
---@return integer|nil row
---@return integer|nil col
---@return string|nil err
function M.simulate_movements(lines, start_row, start_col, seq)
  if not seq or seq == "" then
    return start_row, start_col, nil
  end
  local lines_payload = core.encode_line_array(lines, "lines")
  local result_str = core.slice_to_string(lib.vf_simulate_movements(
    lines_payload,
    #lines_payload,
    start_row,
    start_col,
    seq,
    #seq
  ))
  if result_str:sub(1, 6) == "ERROR:" then
    return nil, nil, result_str
  end
  local parts = vim.split(result_str, core.EVENT_FIELD_SEP, { plain = true, trimempty = true })
  assert(#parts == 2, "vf_simulate_movements returned malformed payload")
  return tonumber(parts[1]), tonumber(parts[2]), nil
end

return M
