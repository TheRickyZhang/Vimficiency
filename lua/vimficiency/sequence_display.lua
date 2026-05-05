local config = require("vimficiency.config")
local ffi_lib = require("vimficiency.ffi")

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

local function expand_display_tokens(tokens)
  local out = {}
  for _, token in ipairs(tokens or {}) do
    out[#out + 1] = {
      text = token.kind == "typed"
        and display_typed_text(token.text)
        or display_key_notation(token.text),
      kind = token.kind,
    }
  end
  return out
end

local function section_tokens(tokens)
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

  local tokens, err = ffi_lib.tokenize_sequence(seq)
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
