local ffi_sequence = require("vimficiency.ffi.sequence")

local M = {}

-- Tokens carry a `kind` tagged by the C++ parser. This module owns the
-- sequence string -> animation-step conversion so replay UI code does not
-- maintain parser fallback state.

local FALLBACK_FOLLOW_BARE = {
  f = true, F = true, t = true, T = true, r = true,
  m = true, ["'"] = true, ["`"] = true, ["@"] = true,
}

local FALLBACK_INSERT_BARE = {
  i = true, I = true, a = true, A = true, o = true, O = true,
  s = true, S = true, R = true, C = true, cc = true,
}

local FALLBACK_VISUAL_BARE = {
  v = true, V = true, ["<C-v>"] = true, gh = true, gH = true,
}

---@param text string
---@return "movement"|"change"|"visual"
local function classify_fallback(text)
  local bare = text:gsub("^%d+", "")
  if FALLBACK_INSERT_BARE[bare] then return "change" end
  if FALLBACK_VISUAL_BARE[bare] then return "visual" end
  if bare:sub(1, 1) == "c" and #bare > 1 then return "change" end
  return "movement"
end

---@param seq string
---@return string[]
local function split_sequence_chars(seq)
  local chars = {}
  local i = 1
  while i <= #seq do
    if seq:sub(i, i) == "<" then
      local close = seq:find(">", i, true)
      if close then
        chars[#chars + 1] = seq:sub(i, close)
        i = close + 1
      else
        chars[#chars + 1] = seq:sub(i, i)
        i = i + 1
      end
    else
      chars[#chars + 1] = seq:sub(i, i)
      i = i + 1
    end
  end
  return chars
end

---@param seq string
---@return VF.Sequence.Token[]
local function fallback_tokens(seq)
  local chars = split_sequence_chars(seq)
  local fallback = {}
  local k = 1
  while k <= #chars do
    local text = chars[k]
    local bare = text:gsub("^%d+", "")
    if #bare == 1 and FALLBACK_FOLLOW_BARE[bare] and k < #chars then
      text = text .. chars[k + 1]
      k = k + 2
    else
      k = k + 1
    end
    fallback[#fallback + 1] = { text = text, kind = classify_fallback(text) }
  end
  return fallback
end

local CHUNK_SIZE = 4

---@param text string
---@return string[]
local function typed_chunks(text)
  local chunks = {}
  local chunk = ""
  local chunk_units = 0

  local function flush_chunk()
    if chunk == "" then return end
    chunks[#chunks + 1] = chunk
    chunk = ""
    chunk_units = 0
  end

  local i = 1
  while i <= #text do
    if text:sub(i, i) == "<" then
      local close = text:find(">", i, true)
      if close then
        flush_chunk()
        chunks[#chunks + 1] = text:sub(i, close)
        i = close + 1
      else
        chunk = chunk .. text:sub(i, i)
        chunk_units = chunk_units + 1
        i = i + 1
      end
    else
      chunk = chunk .. text:sub(i, i)
      chunk_units = chunk_units + 1
      i = i + 1
    end

    if chunk_units >= CHUNK_SIZE then
      flush_chunk()
    end
  end

  flush_chunk()
  return chunks
end

--- Tokenize a sequence for animation. Returns kinded tokens; consumers use
--- `.text` for feeding nvim and `.kind` for modal behavior.
---@param seq string
---@return VF.Sequence.Token[] tokens
function M.for_animation(seq)
  local tokens, err = ffi_sequence.tokenize_sequence(seq)
  if err or not tokens or #tokens == 0 then
    tokens, err = ffi_sequence.tokenize_movements(seq)
    if err or not tokens or #tokens == 0 then
      return fallback_tokens(seq)
    end
  end

  local expanded = {}
  for _, tok in ipairs(tokens) do
    if tok.kind == "typed" then
      for _, chunk in ipairs(typed_chunks(tok.text)) do
        expanded[#expanded + 1] = { text = chunk, kind = "typed" }
      end
    else
      expanded[#expanded + 1] = tok
    end
  end
  return expanded
end

return M
