local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local OVERRIDE_FIELDS = { "base", "count_slope", "span_slope" }
local OVERRIDE_FIELD_SET = { base = true, count_slope = true, span_slope = true }

---@return "ok"|"unknown"|"type_error" status
---@return string|nil err
local function try_assign(cdata, key, value)
  if not pcall(function() local _ = cdata[key] end) then
    return "unknown"
  end
  local ok, err = pcall(function() cdata[key] = value end)
  if not ok then
    return "type_error", err
  end
  return "ok"
end

local function apply_scalars(src, dst, key_prefix, strict)
  local consumed = {}
  for k, v in pairs(src) do
    if type(v) ~= "table" then
      local status, err = try_assign(dst, k, v)
      if status == "ok" then
        consumed[k] = true
      elseif status == "type_error" then
        error(string.format("vimfy: invalid value for '%s%s': %s",
          key_prefix or "", tostring(k), tostring(err)))
      elseif strict and status == "unknown" then
        error(string.format("vimfy: unknown config key '%s%s'",
          key_prefix or "", tostring(k)))
      end
    end
  end
  return consumed
end

---@return table<string, true> consumed
function M.configure(user_config)
  local config = lib.vf_get_config()

  local consumed = apply_scalars(user_config, config)

  if user_config.weights then
    consumed.weights = true
    apply_scalars(user_config.weights, config.weights, "weights.", true)
  end

  if user_config.keys then
    consumed.keys = true
    for key_index, info in pairs(user_config.keys) do
      config.keys[key_index].hand = info.hand
      config.keys[key_index].finger = info.finger
      config.keys[key_index].base_cost = info.base_cost
    end
  end

  if user_config.count_penalty_overrides then
    consumed.count_penalty_overrides = true
    if user_config.use_count_penalty_overrides == nil then
      config.use_count_penalty_overrides = true
    end

    for i = 0, lib.VF_COUNT_CLASS_COUNT - 1 do
      local dst = config.count_penalty_overrides[i]
      for _, f in ipairs(OVERRIDE_FIELDS) do
        dst["has_" .. f] = false
      end
    end

    for class_key, override in pairs(user_config.count_penalty_overrides) do
      local class_index
      if type(class_key) == "number" then
        class_index = class_key
      else
        class_index = core.CountClass[class_key]
      end

      if class_index == nil then
        error("Unknown count penalty class: " .. tostring(class_key))
      end
      if class_index < 0 or class_index >= lib.VF_COUNT_CLASS_COUNT then
        error("Count penalty class out of range: " .. tostring(class_key))
      end

      for field in pairs(override) do
        if not OVERRIDE_FIELD_SET[field] then
          error(string.format(
            "vimfy: unknown count_penalty_overrides[%s] key '%s' (allowed: base, count_slope, span_slope)",
            tostring(class_key), tostring(field)))
        end
      end

      local dst = config.count_penalty_overrides[class_index]
      for _, f in ipairs(OVERRIDE_FIELDS) do
        if override[f] ~= nil then
          dst["has_" .. f] = true
          dst[f] = override[f]
        end
      end
    end
  end

  lib.vf_apply_config()
  return consumed
end

function M.reset_config()
  lib.vf_reset_config()
end

return M
