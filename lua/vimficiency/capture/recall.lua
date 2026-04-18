local key_tracking = require("vimficiency.capture.key_tracking")
local session_store = require("vimficiency.session.store")

local M = {}

function M.install()
  key_tracking.attach_global(function(event)
    if not vim.api.nvim_buf_is_valid(event.buf) then return end
    if vim.bo[event.buf].buftype ~= "" then return end
    if not vim.api.nvim_win_is_valid(event.win) then return end
    session_store.ingest_recall_event(event)
  end, "recall_capture")
end

return M
