-- Pure sequence-manipulation helper used by the staged-mode header:
--   section_sequence: split a Vim sequence into ordered motion / edit stages
--
-- No session state; safe to require from any module.
local ffi_lib = require("vimficiency.ffi")

local M = {}

---Section a full command sequence into ordered stages. Each stage is either
---a `motion` phase (a run of motion/visual tokens) or an `edit` phase (one
---Change/Delete atom with its TypedText + Escape suffix).
---@param seq string
---@return { kind: "motion"|"edit", text: string, tokens: VimficiencyToken[] }[]
function M.section_sequence(seq)
  if not seq or seq == "" then return {} end
  local tokens = ffi_lib.tokenize_sequence(seq) or {}
  local sections = {}
  local cur_motions = {}
  local function flush_motions()
    if #cur_motions == 0 then return end
    local parts = {}
    for _, t in ipairs(cur_motions) do parts[#parts + 1] = t.text end
    sections[#sections + 1] = {
      kind = "motion",
      text = table.concat(parts),
      tokens = cur_motions,
    }
    cur_motions = {}
  end
  local i = 1
  while i <= #tokens do
    local tok = tokens[i]
    if tok.kind == "change" or tok.kind == "delete" then
      flush_motions()
      local edit_tokens = { tok }
      i = i + 1
      while i <= #tokens
            and (tokens[i].kind == "typed" or tokens[i].kind == "escape") do
        edit_tokens[#edit_tokens + 1] = tokens[i]
        i = i + 1
      end
      local parts = {}
      for _, t in ipairs(edit_tokens) do parts[#parts + 1] = t.text end
      sections[#sections + 1] = {
        kind = "edit",
        text = table.concat(parts),
        tokens = edit_tokens,
      }
    else
      cur_motions[#cur_motions + 1] = tok
      i = i + 1
    end
  end
  flush_motions()
  return sections
end

return M
