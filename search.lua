local config = require("vim_opt.config")
local effort = require("vim_opt.effort")

local M = {}

function M.bfs(len, cost)
  local q = {}
  for _ = 1, len do
    local nq = {}
    for s in q do
      for motion in config.motions do
        local c = effort.
        nq.add(s + c)
      end
    end
    q = nq
  end
  return q

  local function dfs(curr, cost)
    for c in config.motions do
      if 
end

local function dfs(curr, cost, res)
  

return M
