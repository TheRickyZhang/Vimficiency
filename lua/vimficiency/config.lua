-- lua/vimficiency/config.lua
-- Shared config module to avoid circular dependencies

local M = {
  RESULTS_CALCULATED = 10, -- Should be >= RESULTS_SAVED, at most 20.
  RESULTS_SAVED = 3, -- should be in [1, 8], otherwise too much overhead
  SLICE_PADDING = 5,
  SLICE_EXPAND_TO_PARAGRAPH = false,
  MAX_SEARCH_LINES = 500,
}

return M
