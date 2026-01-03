-- lua/vimficiency/config.lua
-- Shared config module to avoid circular dependencies

local M = {
  RESULTS_CALCULATED = 20, -- Should be >= RESULTS_SAVED, at most 20.
  RESULTS_SAVED = 5, -- should be in [1, 8], otherwise too much overhead
  SLICE_PADDING = 5,
  SLICE_EXPAND_TO_PARAGRAPH = false,
  MAX_SEARCH_LINES = 500,
  KEY_SESSION_CAPACITY = 10, -- Max rolling key sessions (aliases: 1-N)
}

return M
