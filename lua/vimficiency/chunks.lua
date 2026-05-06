-- Chunk-list utilities shared between the explore header (virt_lines on
-- the scratch buffer) and the play panes (virt_lines on each replay
-- buffer). A "chunk list" is the `{ {text, hl}, ... }` shape Neovim's
-- virt_lines API consumes: each line of virtual text is one such list.
local M = {}

---@param chunks table[]
---@return integer
function M.display_width(chunks)
  local w = 0
  for _, chunk in ipairs(chunks) do
    w = w + vim.fn.strdisplaywidth(chunk[1])
  end
  return w
end

---Pad `chunks` with a trailing space chunk so its display width reaches
---`target_width`. Returns the input unchanged if already wide enough.
---@param chunks table[]
---@param target_width integer
---@return table[]
function M.pad_to(chunks, target_width)
  local display = M.display_width(chunks)
  if display >= target_width then return chunks end
  local out = {}
  for _, chunk in ipairs(chunks) do out[#out + 1] = chunk end
  out[#out + 1] = { string.rep(" ", target_width - display), "Normal" }
  return out
end

---Wrap a chunk list to `width` display columns, splitting chunks at
---grapheme boundaries when needed. Returns one chunk list per output
---line, preserving each chunk's highlight group.
---@param chunks table[]
---@param width integer
---@return table[][]
function M.wrap(chunks, width)
  width = math.max(1, width)
  local lines = {}
  local line = {}
  local line_width = 0

  local function push_line()
    lines[#lines + 1] = line
    line = {}
    line_width = 0
  end

  for _, chunk in ipairs(chunks) do
    local text = chunk[1]
    local hl = chunk[2]
    local total_chars = vim.fn.strchars(text)
    local start = 0

    while start < total_chars do
      if line_width >= width then push_line() end

      local piece_chars = 0
      local piece = ""
      while start + piece_chars < total_chars do
        local next_piece = vim.fn.strcharpart(text, start, piece_chars + 1)
        local next_width = vim.fn.strdisplaywidth(next_piece)
        if piece_chars > 0 and line_width + next_width > width then break end
        piece_chars = piece_chars + 1
        piece = next_piece
        if line_width + next_width >= width then break end
      end

      if piece_chars == 0 then
        piece_chars = 1
        piece = vim.fn.strcharpart(text, start, 1)
      end

      line[#line + 1] = { piece, hl }
      line_width = line_width + vim.fn.strdisplaywidth(piece)
      start = start + piece_chars
    end
  end

  if #line > 0 or #lines == 0 then push_line() end
  return lines
end

return M
