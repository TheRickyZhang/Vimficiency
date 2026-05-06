local M = {}

---@class VF.Explore.InsertContinuationChunk
---@field kind "key"|"literal"
---@field text string

---@class VF.Explore.InsertContinuation
---@field chunks VF.Explore.InsertContinuationChunk[]
---@field typed string
---@field correct_prefix string
---@field literal_suffix string
---@field backspace_count integer
---@field is_complete boolean

local function common_prefix_len(a, b)
  local limit = math.min(#a, #b)
  local len = 0
  while len < limit and a:sub(len + 1, len + 1) == b:sub(len + 1, len + 1) do
    len = len + 1
  end
  return len
end

---@return VF.Explore.InsertContinuation
function M.empty()
  return {
    chunks = {},
    typed = "",
    correct_prefix = "",
    literal_suffix = "",
    backspace_count = 0,
    is_complete = true,
  }
end

---@param literal_target string
---@param typed_so_far string
---@return VF.Explore.InsertContinuation
function M.plan(literal_target, typed_so_far)
  local prefix_len = common_prefix_len(literal_target, typed_so_far)
  local backspace_count = #typed_so_far - prefix_len
  local literal_suffix = literal_target:sub(prefix_len + 1)
  local chunks = {}
  for _ = 1, backspace_count do
    chunks[#chunks + 1] = { kind = "key", text = "<BS>" }
  end
  if literal_suffix ~= "" then
    chunks[#chunks + 1] = { kind = "literal", text = literal_suffix }
  end

  return {
    chunks = chunks,
    typed = typed_so_far,
    correct_prefix = typed_so_far:sub(1, prefix_len),
    literal_suffix = literal_suffix,
    backspace_count = backspace_count,
    is_complete = #chunks == 0,
  }
end

M._common_prefix_len = common_prefix_len

return M
