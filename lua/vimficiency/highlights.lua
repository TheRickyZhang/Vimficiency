-- Shared Vimficiency highlight groups.
--
-- This module is the single canonical place for highlight names + styles
-- used across Vimficiency UIs. Keep it narrow: semantic groups only (what
-- the UI is trying to say), not literal colors. Actual colors either link
-- to built-in groups (so they respect the user's colorscheme) or are
-- computed in UI-local helpers that layer on top (see explore/highlights.lua
-- for the alpha-blended rank palette).
--
-- Every group is installed with `default = true`, so users can override any
-- of them with their own `:highlight` commands without us clobbering them
-- on the next refresh.
--
-- Callers:
--   - require this module at module scope to pick up the names.
--   - call `M.refresh()` before first use if colorscheme may have changed
--     since Vimficiency loaded. (The module calls it once at require time
--     as a convenience.)

local v = vim.api

local M = {}

-- =============================================================================
-- Names — semantic groups exposed to callers
-- =============================================================================

-- Picker (session picker float)
M.PICKER_PANE_ACTIVE   = "VimficiencyPickerPaneActive"   -- currently-selected pane label
M.PICKER_PANE_INACTIVE = "VimficiencyPickerPaneInactive" -- the other pane label
M.PICKER_HEADER_DIM    = "VimficiencyPickerHeaderDim"    -- labels like "pane:" / "sort:"
M.PICKER_SECTION       = "VimficiencyPickerSection"      -- "── Category ──" dividers

-- Simulate / replay (side-by-side sequence playback)
M.REPLAY_CURRENT       = "VimficiencyReplayCurrent"       -- token at the current replay step
M.REPLAY_CURSOR        = "VimficiencyReplayCursor"        -- cursor cell in normal mode
M.REPLAY_CURSOR_INSERT = "VimficiencyReplayCursorInsert"  -- cursor cell in insert mode
M.REPLAY_CURSOR_VISUAL = "VimficiencyReplayCursorVisual"  -- cursor / visual selection
M.REPLAY_ACTIVE        = "VimficiencyReplayActive"        -- active window's label

-- =============================================================================
-- Install
-- =============================================================================

---(Re-)install every highlight group this module owns. Safe to call on every
---UI open so a colorscheme change between loads is picked up. `default = true`
---means user overrides survive.
function M.refresh()
  v.nvim_set_hl(0, M.PICKER_PANE_ACTIVE,   { link = "PmenuSel",   default = true })
  v.nvim_set_hl(0, M.PICKER_PANE_INACTIVE, { link = "Comment",    default = true })
  v.nvim_set_hl(0, M.PICKER_HEADER_DIM,    { link = "NonText",    default = true })
  v.nvim_set_hl(0, M.PICKER_SECTION,       { link = "Title",      default = true })

  v.nvim_set_hl(0, M.REPLAY_CURRENT,       { link = "IncSearch",  default = true })
  v.nvim_set_hl(0, M.REPLAY_CURSOR,        { link = "Search",     default = true })
  v.nvim_set_hl(0, M.REPLAY_CURSOR_INSERT, { link = "DiffAdd",    default = true })
  v.nvim_set_hl(0, M.REPLAY_CURSOR_VISUAL, { link = "Visual",     default = true })
  v.nvim_set_hl(0, M.REPLAY_ACTIVE,        { link = "Title",      default = true })
end

M.refresh()

return M
