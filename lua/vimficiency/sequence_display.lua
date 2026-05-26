local config = require("vimficiency.config")
local ffi_display = require("vimficiency.ffi.display")

local M = {}

local function resolved_opts(opts)
  local defaults = config.sequence_display or {}
  opts = opts or {}
  local tokenize = defaults.tokenize ~= false
  local sectionize = defaults.sectionize ~= false
  if opts.tokenize ~= nil then
    tokenize = opts.tokenize
  end
  if opts.sectionize ~= nil then
    sectionize = opts.sectionize
  end
  return {
    tokenize = tokenize,
    sectionize = sectionize,
  }
end

local function display_token_text(token)
  return token.kind == "typed"
    and ffi_display.sequence_display_typed_text(token.text, true)
    or ffi_display.sequence_display_key_notation(token.text)
end

local function join_token_chunks(tokens, tokenize)
  local chunks = {}
  for i, token in ipairs(tokens) do
    local prev = tokens[i - 1]
    if tokenize and i > 1 and not (prev.kind == "typed" and token.kind == "typed") then
      chunks[#chunks + 1] = { " ", "Normal" }
    end
    chunks[#chunks + 1] = { token.text, token.hl or "Normal" }
  end
  return chunks
end

local section_tokens

local function chunked_token_lines(tokens, tokenize, sectionize)
  if sectionize then
    local sections = section_tokens(tokens)
    if #sections > 1 then
      local lines = {}
      for _, section in ipairs(sections) do
        lines[#lines + 1] = join_token_chunks(section.tokens, tokenize)
      end
      return lines
    end
  end

  return { join_token_chunks(tokens, tokenize) }
end

function section_tokens(tokens)
  local sections = {}
  local current_movement = {}
  local edit_start = { change = true, delete = true }
  local edit_continuation = {
    change = true,
    delete = true,
    typed = true,
    escape = true,
  }

  local function flush_movement()
    if #current_movement == 0 then return end
    sections[#sections + 1] = {
      kind = "movement",
      tokens = current_movement,
    }
    current_movement = {}
  end

  local i = 1
  while i <= #tokens do
    local token = tokens[i]
    if edit_start[token.kind] then
      flush_movement()
      local edit_tokens = {}
      while i <= #tokens and edit_continuation[tokens[i].kind] do
        edit_tokens[#edit_tokens + 1] = tokens[i]
        i = i + 1
      end
      sections[#sections + 1] = {
        kind = "edit",
        tokens = edit_tokens,
      }
    else
      current_movement[#current_movement + 1] = token
      i = i + 1
    end
  end

  flush_movement()
  return sections
end

function M.lines(seq, opts)
  return ffi_display.sequence_display_lines(seq, opts)
end

function M.inline(seq, opts)
  return ffi_display.sequence_display_inline(seq, opts)
end

---@param tokens VF.Sequence.Token[]
---@param opts { tokenize?: boolean, sectionize?: boolean, highlight_index?: integer, highlight_group?: string }?
---@return table[][]
function M.chunked_lines_for_tokens(tokens, opts)
  local resolved = resolved_opts(opts)
  opts = opts or {}

  if not tokens or #tokens == 0 then
    return { { { "", "Normal" } } }
  end

  local display_tokens = {}
  for i, token in ipairs(tokens) do
    local index = token.index or i
    display_tokens[#display_tokens + 1] = {
      text = display_token_text(token),
      kind = token.kind,
      index = index,
      hl = opts.highlight_index == index
        and (opts.highlight_group or "Visual")
        or "Normal",
    }
  end

  return chunked_token_lines(display_tokens, resolved.tokenize, resolved.sectionize)
end

---@param text string
---@param opts { tokenize?: boolean }?
---@return string
function M.typed_text_inline(text, opts)
  return ffi_display.sequence_display_typed_text(text, true)
end

---@param text string
---@param opts { tokenize?: boolean }?
---@return string
function M.literal_typed_text_inline(text, opts)
  return ffi_display.sequence_display_typed_text(text, false)
end

---@param chunks { kind: string, text: string }[]
---@param opts { tokenize?: boolean }?
---@return string
function M.typed_chunks_inline(chunks, opts)
  return ffi_display.sequence_display_typed_chunks(chunks, opts)
end

function M.prefixed_lines(prefix, seq, opts, suffix)
  local rendered = M.lines(seq, opts)
  local first = rendered[1] or ""
  local out = { prefix .. first .. (suffix or "") }
  local continuation = string.rep(" ", #prefix)
  for i = 2, #rendered do
    out[#out + 1] = continuation .. rendered[i]
  end
  return out
end

return M
