local ffi_optimizer    = require("vimficiency.ffi.optimizer")
local settings_profile = require("vimficiency.settings_profile")
local panel_render     = require("vimficiency.explore.render.panel")
local settings         = require("vimficiency.explore.settings")

local M = {}

---@param scope "shared"|"nav"|"transform"|"composition"
---@param key string
---@return any
local function default_for(scope, key)
  local defaults = ffi_optimizer.get_optimizer_defaults()
  local scope_defaults = assert(defaults[scope],
    "vimfy explore: missing optimizer defaults for " .. scope)
  local value = scope_defaults[key]
  assert(value ~= nil, "vimfy explore: missing optimizer default for " .. scope .. "." .. key)
  return value
end

---@param a VF.Explore.Active
---@param refresh_ui fun()
---@return table[]
function M.build(a, refresh_ui)
  -- LuaJIT 5.1 lacks math.maxinteger.
  local DEDUP_OFF = 2147483647
  local store = settings.settings_store()
  local function dedup_toggle(scope, key)
    return
      function()
        local value = store[scope][key]
        if value == nil then value = default_for(scope, key) end
        return value == 1
      end,
      function(on) settings.update_setting(a, scope, key, on and 1 or DEDUP_OFF) end
  end
  local motion_get, motion_set = dedup_toggle("nav", "maxResultsPerEndPos")
  local edit_get, edit_set = dedup_toggle("transform", "maxResultsPerStartPos")

  local function opt_get(scope, key, legacy_key)
    return function()
      local v = store[scope][key]
      if v ~= nil then return v end
      if legacy_key ~= nil then
        v = store[scope][legacy_key]
        if v ~= nil then return v end
      end
      return default_for(scope, key)
    end
  end
  local function opt_set(scope, key)
    return function(value)
      settings.update_setting(a, scope, key, value)
      if scope == "shared" or scope == "composition" then
        a.plan_reconfigure_pending = true
      end
    end
  end

  local entries = { { kind = "hint", text = "── Display & UI ──" } }
  for _, spec in ipairs(settings.VIEW_SETTINGS) do
    local key = spec.key
    local entry = {
      kind = "setting", label = spec.label, value_kind = spec.type,
      get = function() return a[key] end,
      set = function(value) settings.update_setting(a, "view", key, value) end,
    }
    if spec.type == "enum" then
      entry.values = spec.values
    elseif spec.type == "int" then
      entry.min = spec.min
      entry.max = type(spec.max) == "function" and spec.max(a) or spec.max
    end
    table.insert(entries, entry)
  end

  return vim.list_extend(entries, {
    { kind = "separator" },
    { kind = "hint", text = "── Optimizer (shared) ──" },
    { kind = "setting",
      label = "Max results",
      value_kind = "int", min = 1, max = 100,
      get = opt_get("shared", "maxResults"), set = opt_set("shared", "maxResults") },
    { kind = "setting",
      label = "Max nodes popped",
      value_kind = "int", min = 1000, max = 500000, step = 1000,
      get = opt_get("shared", "maxNodesPopped"), set = opt_set("shared", "maxNodesPopped") },
    { kind = "setting",
      label = "Explore factor",
      value_kind = "float", min = 1.0, max = 10.0, step = 0.1,
      get = opt_get("shared", "exploreFactor"), set = opt_set("shared", "exploreFactor") },
    { kind = "setting",
      label = "Effort weight",
      value_kind = "float", min = 0.0, max = 5.0, step = 0.1,
      get = opt_get("shared", "effortWeight"), set = opt_set("shared", "effortWeight") },
    { kind = "setting",
      label = "Distance weight",
      value_kind = "float", min = 0.0, max = 5.0, step = 0.1,
      get = opt_get("shared", "distanceWeight"), set = opt_set("shared", "distanceWeight") },
    { kind = "setting",
      label = "Min prefix count",
      value_kind = "int", min = 2, max = 16,
      get = opt_get("shared", "minPrefixCount"), set = opt_set("shared", "minPrefixCount") },
    { kind = "setting",
      label = "Max prefix count",
      value_kind = "int", min = 0, max = 32,
      get = opt_get("shared", "maxPrefixCount"), set = opt_set("shared", "maxPrefixCount") },

    { kind = "separator" },
    { kind = "hint", text = "── Nav ──" },
    { kind = "setting",
      label = "Per-cell dedup",
      value_kind = "bool", get = motion_get, set = motion_set },
    { kind = "setting",
      label = "fMotion threshold",
      value_kind = "int", min = 0, max = 20,
      get = opt_get("nav", "fMotionThreshold"),
      set = opt_set("nav", "fMotionThreshold") },
    { kind = "setting",
      label = "Directional pruning",
      value_kind = "bool",
      get = opt_get("nav", "useDirectionalPruning"),
      set = opt_set("nav", "useDirectionalPruning") },

    { kind = "separator" },
    { kind = "hint", text = "── Transform ──" },
    { kind = "setting",
      label = "Per-start dedup",
      value_kind = "bool", get = edit_get, set = edit_set },

    -- fMotionThreshold/useDirectionalPruning are physically separate
    -- fields on Nav and Composition, not one shared knob.
    { kind = "separator" },
    { kind = "hint", text = "── Composition ──" },
    { kind = "setting",
      label = "fMotion threshold",
      value_kind = "int", min = 0, max = 20,
      get = opt_get("composition", "fMotionThreshold"),
      set = opt_set("composition", "fMotionThreshold") },
    { kind = "setting",
      label = "Directional pruning",
      value_kind = "bool",
      get = opt_get("composition", "useDirectionalPruning"),
      set = opt_set("composition", "useDirectionalPruning") },
    { kind = "setting",
      label = "Overshoot penalty",
      value_kind = "float", min = 0.0, max = 10.0, step = 0.5,
      get = opt_get("composition", "overshootPenalty"),
      set = opt_set("composition", "overshootPenalty") },
    { kind = "setting",
      label = "Diff open penalty",
      value_kind = "float", min = 0.0, max = 40.0, step = 0.5,
      get = opt_get("composition", "diffOpenPenalty", "treeDiffOpenPenalty"),
      set = opt_set("composition", "diffOpenPenalty") },
    { kind = "setting",
      label = "Nav padding above",
      value_kind = "int", min = 0, max = 10,
      get = opt_get("composition", "navPaddingAbove"),
      set = opt_set("composition", "navPaddingAbove") },
    { kind = "setting",
      label = "Nav padding below",
      value_kind = "int", min = 0, max = 10,
      get = opt_get("composition", "navPaddingBelow"),
      set = opt_set("composition", "navPaddingBelow") },
    { kind = "setting",
      label = "Diff algorithm",
      value_kind = "int", min = 0, max = 1,  -- 0 VimDiff, 1 MyersDiff
      get = opt_get("composition", "diffAlgorithm"),
      set = opt_set("composition", "diffAlgorithm") },

    { kind = "separator" },
    { kind = "action",
      label = "reset to default settings",
      run = function()
        settings_profile.clear("explore")
        settings.invalidate_cache()
        local s = settings.settings_store()
        for key, val in pairs(s.view) do a[key] = val end
        a.plan_reconfigure_pending = true
        refresh_ui()
        panel_render.refresh(a)
        vim.notify("vimfy explore: settings reset to defaults",
          vim.log.levels.INFO)
      end },
    { kind = "separator" },
    { kind = "hint", text = "←/→ step · ⏎/i input" },
  })
end

return M
