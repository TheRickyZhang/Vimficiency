local config = require("vimficiency.config")
local ffi_sequence = require("vimficiency.ffi.sequence")

local M = {}

local KEY_NOTATION_GLYPHS = {
  ["<Space>"] = "␣",
  ["<Tab>"] = "⇥",
  ["<CR>"] = "↵",
  ["<Enter>"] = "↵",
  ["<Return>"] = "↵",
  ["<lt>"] = "<",
  ["<LT>"] = "<",
}

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

local function join_tokens(tokens, tokenize)
  local parts = {}
  for _, token in ipairs(tokens) do
    parts[#parts + 1] = token.text
  end
  if tokenize then
    return table.concat(parts, " ")
  end
  return table.concat(parts)
end

local function display_key_notation(text)
  local out = {}
  local i = 1
  while i <= #text do
    if text:sub(i, i) == "<" then
      local close = text:find(">", i, true)
      if close then
        local special = text:sub(i, close)
        out[#out + 1] = KEY_NOTATION_GLYPHS[special] or special
        i = close + 1
      else
        out[#out + 1] = text:sub(i, i)
        i = i + 1
      end
    else
      out[#out + 1] = text:sub(i, i)
      i = i + 1
    end
  end
  return table.concat(out)
end

local function display_plain_typed_text(text)
  return text:gsub(" ", "␣"):gsub("\t", "⇥"):gsub("\n", "↵")
end

local function split_key_notation_typed_text(text)
  local parts = {}
  local plain = {}
  local i = 1

  local function flush_plain()
    if #plain == 0 then return end
    parts[#parts + 1] = table.concat(plain)
    plain = {}
  end

  while i <= #text do
    if text:sub(i, i) == "<" then
      local close = text:find(">", i, true)
      if close then
        flush_plain()
        parts[#parts + 1] = text:sub(i, close)
        i = close + 1
      else
        plain[#plain + 1] = text:sub(i, i)
        i = i + 1
      end
    else
      plain[#plain + 1] = text:sub(i, i)
      i = i + 1
    end
  end

  flush_plain()
  return parts
end

local function display_typed_text(text)
  local parts = split_key_notation_typed_text(text)
  for i, part in ipairs(parts) do
    if part:sub(1, 1) == "<" and part:sub(-1) == ">" then
      parts[i] = display_key_notation(part)
    else
      parts[i] = display_plain_typed_text(part)
    end
  end
  return table.concat(parts)
end

local function display_token_text(token)
  return token.kind == "typed"
    and display_typed_text(token.text)
    or display_key_notation(token.text)
end

local function expand_display_tokens(tokens)
  local out = {}
  for i, token in ipairs(tokens or {}) do
    out[#out + 1] = {
      text = display_token_text(token),
      kind = token.kind,
      index = token.index or i,
    }
  end
  return out
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

local function tokenized_lines(seq, opts)
  if not seq or seq == "" then
    return { "" }
  end

  local tokens, err = ffi_sequence.tokenize_sequence(seq)
  if err or not tokens or #tokens == 0 then
    return { display_key_notation(seq) }
  end
  tokens = expand_display_tokens(tokens)

  if opts.sectionize then
    local sections = section_tokens(tokens)
    if #sections > 1 then
      local lines = {}
      for _, section in ipairs(sections) do
        lines[#lines + 1] = join_tokens(section.tokens, opts.tokenize)
      end
      return lines
    end
  end

  return { join_tokens(tokens, opts.tokenize) }
end

function M.lines(seq, opts)
  local resolved = resolved_opts(opts)
  if not resolved.tokenize and not resolved.sectionize then
    return { seq or "" }
  end
  return tokenized_lines(seq, resolved)
end

function M.inline(seq, opts)
  local resolved = resolved_opts(opts)
  resolved.sectionize = false
  return tokenized_lines(seq, resolved)[1] or ""
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
  local resolved = resolved_opts(opts)
  return join_tokens({
    {
      text = display_typed_text(text or ""),
      kind = "typed",
    },
  }, resolved.tokenize)
end

---@param text string
---@param opts { tokenize?: boolean }?
---@return string
function M.literal_typed_text_inline(text, opts)
  local resolved = resolved_opts(opts)
  return join_tokens({
    {
      text = display_plain_typed_text(text or ""),
      kind = "typed",
    },
  }, resolved.tokenize)
end

---@param chunks { kind: string, text: string }[]
---@param opts { tokenize?: boolean }?
---@return string
function M.typed_chunks_inline(chunks, opts)
  local resolved = resolved_opts(opts)
  local tokens = {}
  for _, chunk in ipairs(chunks or {}) do
    local text = chunk.kind == "literal"
      and display_plain_typed_text(chunk.text or "")
      or display_key_notation(chunk.text or "")
    if text ~= "" then
      tokens[#tokens + 1] = { text = text, kind = "typed" }
    end
  end
  return join_tokens(tokens, resolved.tokenize)
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
