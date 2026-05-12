local max = math.max
local min = math.min

local tokenize = require("vimficiency.simulate.tokenize")

local M = {}

---@class VF.Replay.LayoutSlot
---@field is_user boolean
---@field default_rank integer

---@alias VF.Replay.PoolRankKey "user" | integer

---@param pool_arg { user: { seq: string, cost: string? }?, suggestions: { seq: string, cost: string? }[] }
---@return VF.Replay.Pool
function M.build(pool_arg)
  local pool = { user = nil, suggestions = {} }
  if pool_arg.user then
    pool.user = {
      tokens = tokenize.for_animation(pool_arg.user.seq),
      cost = pool_arg.user.cost,
      states = nil,
    }
  end
  for i, sug in ipairs(pool_arg.suggestions or {}) do
    pool.suggestions[i] = {
      tokens = tokenize.for_animation(sug.seq),
      cost = sug.cost,
      states = nil,
    }
  end
  return pool
end

---@param pool VF.Replay.Pool
---@param key VF.Replay.PoolRankKey
---@return VF.Replay.PoolEntry?
function M.entry_by_rank(pool, key)
  if key == "user" then return pool.user end
  return pool.suggestions[key]
end

---@param pool VF.Replay.Pool
---@return integer window_count
---@return boolean include_user
function M.effective_layout(pool)
  local settings = require("vimficiency.play").get_settings()
  local include_user = (settings.include_user_sequence == true) and pool.user ~= nil
  local window_count = max(1, min(4, settings.window_count or 2))
  local suggestion_slots = window_count - (include_user and 1 or 0)
  local total_suggestions = #pool.suggestions
  if suggestion_slots > total_suggestions then
    suggestion_slots = total_suggestions
    window_count = suggestion_slots + (include_user and 1 or 0)
  end
  if window_count < 1 then window_count = 1 end
  return window_count, include_user
end

---@param pool VF.Replay.Pool
---@return VF.Replay.LayoutSlot[]
function M.layout_plan(pool)
  local window_count, include_user = M.effective_layout(pool)
  local plan = {}
  if include_user then
    plan[#plan + 1] = { is_user = true, default_rank = 0 }
  end
  local suggestion_slots = window_count - (include_user and 1 or 0)
  for rank = 1, suggestion_slots do
    plan[#plan + 1] = { is_user = false, default_rank = rank }
  end
  return plan
end

---@param plan VF.Replay.LayoutSlot[]
---@return VF.Replay.PoolRankKey[]
function M.ranks_for_plan(plan)
  local ranks = {}
  for _, slot in ipairs(plan) do
    ranks[#ranks + 1] = slot.is_user and "user" or slot.default_rank
  end
  return ranks
end

return M
