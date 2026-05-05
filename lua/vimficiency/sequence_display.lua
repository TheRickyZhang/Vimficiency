local config = require("vimficiency.config")
local ffi_lib = require("vimficiency.ffi")

local M = {}

-- Display-only glyphs; keep bracket notation for anything fed to Neovim.
-- Bare replacements do not touch forms like <C-Space> or <S-Tab>.
local GLYPHS = {
  ["<Space>"] = "␣",
  ["<Tab>"]   = "⇥",
  ["<CR>"]    = "↵",
}

local function glyphify(text)
  for bracket, glyph in pairs(GLYPHS) do
    text = text:gsub(bracket, glyph)
  end
  return text
end

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
    parts[#parts + 1] = glyphify(token.text)
  end
  if tokenize then
    return table.concat(parts, " ")
  end
  return table.concat(parts)
end

local function section_tokens(tokens)
  local sections = {}
  local current_movement = {}

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
    if token.kind == "change" or token.kind == "delete" then
      flush_movement()
      local edit_tokens = { token }
      i = i + 1
      while i <= #tokens
            and (tokens[i].kind == "typed" or tokens[i].kind == "escape") do
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
    return { seq }
  end

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
