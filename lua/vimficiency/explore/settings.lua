local config           = require("vimficiency.config")
local settings_profile = require("vimficiency.settings_profile")

local M = {}

local EXPLORE_SCHEMA_VERSION = 1
M.EXPLORE_SCHEMA_VERSION = EXPLORE_SCHEMA_VERSION

local DISPLAY_MODES = { "off", "highlight", "inplace", "above", "below" }
M.DISPLAY_MODES = DISPLAY_MODES

local RECOMMENDATION_SORT_MODES = { "effort", "distance", "score" }
M.RECOMMENDATION_SORT_MODES = RECOMMENDATION_SORT_MODES

local SHARED_OPTIMIZER_KEYS = {
  maxResults = true,
  maxNodesPopped = true,
  exploreFactor = true,
  effortWeight = true,
  distanceWeight = true,
  minPrefixCount = true,
  maxPrefixCount = true,
}
M.SHARED_OPTIMIZER_KEYS = SHARED_OPTIMIZER_KEYS

---@class VF.Explore.ViewSettingSpec
---@field key string
---@field type "enum"|"int"|"bool"
---@field label string
---@field default any
---@field values? string[]
---@field min? integer
---@field max? integer|fun(a:VF.Explore.Active):integer

-- Source of truth for view-section settings. Drives defaults, persistence
-- filter, write validation, and the modal schema.
---@type VF.Explore.ViewSettingSpec[]
local VIEW_SETTINGS = {
  { key = "display_mode",         type = "enum", label = "Display mode",
    default = "above",            values = DISPLAY_MODES },
  { key = "recommendation_sort",  type = "enum", label = "Suggestion sort",
    default = "effort",           values = RECOMMENDATION_SORT_MODES },
  { key = "recommendation_count", type = "int",  label = "Suggestion count",
    default = 5,                  min = 1, max = 10 },
  { key = "show_user_typed",      type = "bool", label = "Show user typed",
    default = true },
  { key = "show_result_count",    type = "int",  label = "Results shown",
    default = 1,                  min = 0,
    max = function(a) return #a.header_rows.optimal end },
  -- Wall-clock budget for the off-thread composition search at session open.
  -- The worker returns best-so-far past this; 0 = run to the natural caps.
  -- Responsiveness comes from the search being off-thread, not from this bound.
  { key = "compute_deadline_ms",  type = "int",  label = "Compute deadline (ms)",
    default = 2000,               min = 0, max = 60000 },
}
M.VIEW_SETTINGS = VIEW_SETTINGS

local VIEW_SETTING_BY_KEY = {}
for _, spec in ipairs(VIEW_SETTINGS) do VIEW_SETTING_BY_KEY[spec.key] = spec end
M.VIEW_SETTING_BY_KEY = VIEW_SETTING_BY_KEY

-- LSP-only mirror of VIEW_SETTINGS; the runtime table isn't introspectable.
---@class VF.Explore.ViewSettings
---@field display_mode string
---@field recommendation_sort string
---@field recommendation_count integer
---@field show_user_typed boolean
---@field show_result_count integer
---@field compute_deadline_ms integer

---@class VF.Explore.Settings
---@field shared table<string, any>
---@field nav table<string, any>
---@field transform table<string, any>
---@field composition table<string, any>
---@field view VF.Explore.ViewSettings

---@return VF.Explore.Settings
function M.default_settings()
  local view = {}
  for _, spec in ipairs(VIEW_SETTINGS) do
    local user_override = config.explore[spec.key]
    view[spec.key] = user_override ~= nil and user_override or spec.default
  end
  return { shared = {}, nav = {}, transform = {}, composition = {}, view = view }
end

---@param data table
---@return VF.Explore.Settings
function M.load_saved_settings(data)
  local out = M.default_settings()
  for _, scope in ipairs({ "shared", "nav", "transform", "composition" }) do
    if type(data[scope]) == "table" then
      for k, val in pairs(data[scope]) do
        out[scope][k] = val
      end
    end
  end
  if type(data.view) == "table" then
    for k, val in pairs(data.view) do
      if VIEW_SETTING_BY_KEY[k] ~= nil then out.view[k] = val end
    end
  end
  return out
end

---@type VF.Explore.Settings|nil
local current_settings

-- Lazy: defers config.explore reads until after setup() has run.
---@return VF.Explore.Settings
function M.settings_store()
  if current_settings == nil then
    local envelope = settings_profile.load("explore")
    if envelope.version == EXPLORE_SCHEMA_VERSION then
      current_settings = M.load_saved_settings(envelope.data)
    else
      current_settings = M.default_settings()
    end
  end
  return current_settings
end

function M.invalidate_cache()
  current_settings = nil
end

-- Lookup order: per-scope override → shared base override → nil.
---@param key string
---@param scope "nav"|"transform"|"composition"
---@return any|nil
function M.resolve_param(key, scope)
  local store = M.settings_store()
  local scoped = store[scope] and store[scope][key]
  if scoped ~= nil then return scoped end
  if not SHARED_OPTIMIZER_KEYS[key] then return nil end
  return store.shared[key]
end

---@param a VF.Explore.Active
---@param section "shared"|"nav"|"transform"|"composition"|"view"
---@param key string
---@param value any
function M.update_setting(a, section, key, value)
  if section == "view" and VIEW_SETTING_BY_KEY[key] == nil then
    error("vimfy explore: unknown view-section key '" .. tostring(key) .. "'", 0)
  end
  local store = M.settings_store()
  store[section][key] = value
  if section == "view" then a[key] = value end
  settings_profile.save("explore", EXPLORE_SCHEMA_VERSION, store)
end

return M
