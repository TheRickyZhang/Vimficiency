local ffi_explore = require("vimficiency.ffi.explore")

local M = {}

-- Cadence for checking the worker. The search is off-thread, so this only
-- bounds how quickly the panel fills once the result is ready, not the main
-- thread's responsiveness. Implementation detail, not a user-facing setting.
local POLL_INTERVAL_MS = 25

---@class VF.Explore.PollHandle
---@field job_id integer
---@field done boolean
---@field cancelled boolean

--- Polls a pending explore job on a uv timer and resolves it on the main
--- thread. Calls `on_ready(view_id)` exactly once when the worker finishes.
---@param job_id integer
---@param on_ready fun(view_id: integer)
---@return VF.Explore.PollHandle
function M.start(job_id, on_ready)
  local timer = assert(vim.uv.new_timer())
  local handle = { job_id = job_id, done = false, cancelled = false }

  local function close_timer()
    if not timer:is_closing() then
      timer:stop()
      timer:close()
    end
  end
  handle.close_timer = close_timer

  timer:start(0, POLL_INTERVAL_MS, vim.schedule_wrap(function()
    -- Guard against an already-queued tick firing after we resolved/cancelled:
    -- polling a taken job would abort in C++ (g_jobs.get).
    if handle.done or handle.cancelled then return end
    local view_id = ffi_explore.explore_poll(job_id)
    if view_id == nil then return end
    handle.done = true
    close_timer()
    on_ready(view_id)
  end))

  return handle
end

--- Stops polling and cancels the worker if it has not finished. Idempotent.
---@param handle VF.Explore.PollHandle
function M.cancel(handle)
  if handle.cancelled then return end
  handle.cancelled = true
  handle.close_timer()
  if not handle.done then
    ffi_explore.explore_cancel(handle.job_id)
  end
end

return M
