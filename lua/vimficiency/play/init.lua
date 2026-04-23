-- Play-screen UI preferences.
--
-- Mirror of the explore settings pattern:
--   - hardcoded defaults in config.lua's `fields.play`
--   - init.lua declarations merged over by `config.apply`
--   - sidecar file auto-layered on top, mutated on every toggle
--
-- The only things local to a play session are:
--   1. include_user_sequence: whether the user's typed sequence is
--      shown alongside the optimal(s).
--   2. default_result_count: how many optimal_results to surface when
--      the user runs `:Vimfy play <alias>` without an explicit count.
--
-- Settings modal opened via `gs` from
-- the replay grid. Unlike explore, play itself has no persistent
-- single-buffer controller; the settings still live outside the replay
-- state machine.
local config = require("vimficiency.config")
local settings_profile = require("vimficiency.settings_profile")
local settings_ui = require("vimficiency.settings_modal")

local M = {}

-- Session-scoped in-memory store. Seeded lazily from
-- `config.play` + sidecar; mutated by the settings modal.
---@type table|nil
local current_settings

local function settings_store()
  if current_settings == nil then
    current_settings = vim.deepcopy(config.play or {})
    local saved = settings_profile.load("play")
    for k, v in pairs(saved) do
      if current_settings[k] ~= nil or config.play[k] == nil then
        current_settings[k] = v
      end
    end
  end
  return current_settings
end

local function update_setting(key, value)
  local store = settings_store()
  store[key] = value
  settings_profile.save("play", store)
end

---Public accessor — session/init.lua's `simulate()` reads these on
---every invocation to decide whether to include the user sequence and
---how many optimal results to surface.
---@return table  copy of the current settings
function M.get_settings()
  return vim.deepcopy(settings_store())
end

---Build the settings schema and open the generic modal.
---@param opts? { on_change?: fun(), on_close?: fun() }
function M.open_settings(opts)
  opts = opts or {}
  -- Max result count clamped to the grid's window budget (4×2 = 8).
  -- Include-user costs one window, so the functional cap is 7 when
  -- user is included; we let the user set up to 8 and rely on
  -- simulate's own tail-trim to handle overflow.
  local RESULT_MIN, RESULT_MAX = 1, 8

  local schema = {
    { kind = "setting",
      label = "Include user sequence",
      value_kind = "bool",
      get = function() return settings_store().include_user_sequence end,
      set = function(v) update_setting("include_user_sequence", v) end },
    { kind = "setting",
      label = "Default result count",
      value_kind = "int", min = RESULT_MIN, max = RESULT_MAX,
      get = function() return settings_store().default_result_count end,
      set = function(v) update_setting("default_result_count", v) end },
    { kind = "separator" },
    { kind = "action",
      label = "reset to default settings",
      run = function()
        settings_profile.clear("play")
        current_settings = nil
        vim.notify("vimficiency play: settings reset to defaults",
          vim.log.levels.INFO)
      end },
  }
  settings_ui.open(schema, opts.on_change, {
    title = "Play Settings",
    on_close = opts.on_close,
  })
end

return M
