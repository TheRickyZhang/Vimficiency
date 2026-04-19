local key_tracking = require("vimficiency.capture.key_tracking")
local session_store = require("vimficiency.session.store")

local M = {}

--- Install the global recall-capture subscriber. Idempotent: a redundant
--- call (e.g. a second `setup()` invocation) returns false and emits a
--- warning so the double-setup bug surfaces instead of silently leaving
--- recall disabled.
---@return boolean installed  true if the subscriber was newly attached
function M.install()
  local ok = key_tracking.attach_global(function(event)
    if not vim.api.nvim_buf_is_valid(event.buf) then return end
    if vim.bo[event.buf].buftype ~= "" then return end
    if not vim.api.nvim_win_is_valid(event.win) then return end
    session_store.ingest_recall_event(event)
  end, "recall_capture", function(typed_raw)
    -- Multi-key mapping resolution: the LHS keys already flowed through
    -- `ingest_recall_event` into every active recall record. Strip them
    -- so Recall sessions don't record the binding's LHS as motion.
    session_store.strip_recall_pre_resolution(typed_raw)
  end)
  if not ok then
    vim.notify(
      "vimficiency: recall capture already installed (subscriber name " ..
      "'recall_capture' in use). If you called setup() twice, this is " ..
      "harmless — recall is still active from the first call. Otherwise " ..
      "another plugin is colliding on the subscriber name.",
      vim.log.levels.WARN)
  end
  return ok
end

return M
