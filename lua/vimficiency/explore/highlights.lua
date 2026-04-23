-- Rank / target / diff highlight groups used throughout the explore UI.
--
-- Rank groups blend against the Normal bg/fg at resolve time so lower-ranked
-- overlays visually fade toward the background. `refresh()` must be called
-- at every explore-open so a colorscheme change between loads is picked up;
-- the module calls it once at require time as a convenience.
--
-- Target + diff are flat (no blend) — they're meant to stand out.
local v = vim.api

local M = {}

M.TARGET_HL = "VimficiencyExploreTarget"

-- Each rank also carries an alpha in [0.2, 0.8] mapped linearly across the
-- ranks (best = 0.8, worst = 0.2), blended against the Normal group's bg/fg.
local RANK_HL_BASE = {
  { name = "VimficiencyExploreRank1", bg = "#4aff4a", fg = "#000000", alpha = 0.80 },
  { name = "VimficiencyExploreRank2", bg = "#4aff4a", fg = "#000000", alpha = 0.65 },
  { name = "VimficiencyExploreRank3", bg = "#4aff4a", fg = "#000000", alpha = 0.50 },
  { name = "VimficiencyExploreRank4", bg = "#4aff4a", fg = "#000000", alpha = 0.35 },
  { name = "VimficiencyExploreRank5", bg = "#4aff4a", fg = "#000000", alpha = 0.20 },
}

---@param hex string  e.g. "#4aff4a"
local function hex_to_rgb(hex)
  hex = hex:gsub("^#", "")
  return tonumber(hex:sub(1, 2), 16),
         tonumber(hex:sub(3, 4), 16),
         tonumber(hex:sub(5, 6), 16)
end

---@param int integer  a 24-bit packed RGB as returned by nvim_get_hl
local function int_to_hex(int)
  return string.format("#%06x", int)
end

---@param fg string  "#rrggbb"
---@param bg string  "#rrggbb"
---@param alpha number  0..1 — weight given to fg
local function blend_hex(fg, bg, alpha)
  local fr, fgn, fb = hex_to_rgb(fg)
  local br, bgn, bb = hex_to_rgb(bg)
  local r = math.floor(alpha * fr + (1 - alpha) * br + 0.5)
  local g = math.floor(alpha * fgn + (1 - alpha) * bgn + 0.5)
  local b = math.floor(alpha * fb + (1 - alpha) * bb + 0.5)
  return string.format("#%02x%02x%02x", r, g, b)
end

local function normal_colors()
  local ok, hl = pcall(v.nvim_get_hl, 0, { name = "Normal", link = false })
  local bg = (ok and hl and hl.bg) and int_to_hex(hl.bg) or "#1e1e1e"
  local fg = (ok and hl and hl.fg) and int_to_hex(hl.fg) or "#e0e0e0"
  return bg, fg
end

---(Re-)install every highlight group this module owns. Safe to call on
---every explore open — colorscheme changes between loads are picked up.
function M.refresh()
  local base_bg, base_fg = normal_colors()
  for _, entry in ipairs(RANK_HL_BASE) do
    v.nvim_set_hl(0, entry.name, {
      bg = blend_hex(entry.bg, base_bg, entry.alpha),
      fg = blend_hex(entry.fg, base_fg, entry.alpha),
      default = true,
    })
  end
  -- Background-only target: preserves the underlying character's fg so a
  -- word like `main` reads as `main` against the blue backdrop.
  v.nvim_set_hl(0, M.TARGET_HL, { bg = "#2a7fff", default = true })
end

---Name of the hl group for a 1-indexed rank. Ranks beyond the defined set
---collapse to the last (dimmest) rank rather than erroring.
---@param rank integer
---@return string
function M.rank_hl(rank)
  local entry = RANK_HL_BASE[rank] or RANK_HL_BASE[#RANK_HL_BASE]
  return entry.name
end

M.refresh()

return M
