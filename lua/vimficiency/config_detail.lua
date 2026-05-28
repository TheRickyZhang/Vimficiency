-- lua/vimficiency/config_detail.lua
-- Small helpers for config.lua. Keep the public config shape in config.lua;
-- only the imperative merge/validation details live here.

local alias_mod = require("vimficiency.session.alias")

local M = {}

local CPP = {}
local WATCH_DEFAULTS = {
  cooldown_ms = 5000,
}

function M.cpp()
  return CPP
end

function M.is_cpp(value)
  return value == CPP
end

function M.capture_defaults(fields)
  local out = {}
  for name, value in pairs(fields) do
    if type(value) ~= "function" and name:sub(1, 1) ~= "_" and not M.is_cpp(value) then
      out[name] = vim.deepcopy(value)
    end
  end
  return out
end

local function expect_table(path, value)
  if type(value) ~= "table" then
    error(path .. " must be a table, got " .. type(value), 0)
  end
end

local function expect_exact_keys(path, value, allowed)
  for key in pairs(value) do
    if not allowed[key] then
      error(string.format("%s: unknown key '%s'", path, tostring(key)), 0)
    end
  end
end

local function expect_positive_number(path, value)
  if type(value) ~= "number" or value <= 0 then
    error(path .. " must be a positive number", 0)
  end
end

local function expect_non_negative_number(path, value)
  if type(value) ~= "number" or value < 0 then
    error(path .. " must be a non-negative number", 0)
  end
end

local function expect_positive_integer(path, value)
  if type(value) ~= "number" or value <= 0 or value ~= math.floor(value) then
    error(path .. " must be a positive integer", 0)
  end
end

local function expect_recall_alias(path, value)
  if type(value) ~= "string" or not alias_mod.is_recall(value) then
    error(path .. " must be a recall alias (digits, e.g. '50', or digits+s, e.g. '3s')", 0)
  end
end

local function normalize_idle_trigger(path, value)
  expect_table(path, value)
  expect_exact_keys(path, value, { ms = true, window = true })
  expect_positive_number(path .. ".ms", value.ms)
  expect_recall_alias(path .. ".window", value.window)
  return value
end

local function normalize_keys_trigger(path, value)
  expect_table(path, value)
  expect_exact_keys(path, value, { every = true })
  expect_positive_integer(path .. ".every", value.every)
  return value
end

local function normalize_cost_trigger(path, value)
  expect_table(path, value)
  expect_exact_keys(path, value, { m = true, b = true, ms = true, window = true })
  expect_positive_number(path .. ".m", value.m)
  expect_non_negative_number(path .. ".b", value.b)
  expect_positive_number(path .. ".ms", value.ms)
  expect_recall_alias(path .. ".window", value.window)
  return value
end

function M.normalize_watch(raw)
  if raw == nil or raw == false then
    return false
  end

  expect_table("watch", raw)
  expect_exact_keys("watch", raw, { idle = true, cooldown_ms = true })

  local merged = vim.tbl_deep_extend("force", vim.deepcopy(WATCH_DEFAULTS), raw)
  if raw.idle == nil then
    error("watch.idle must be provided when watch is configured", 0)
  end

  expect_table("watch.idle", merged.idle)
  if merged.idle.window ~= nil then
    error(
      "watch.idle.window is not allowed (watch starts manually; no window to scope). " ..
      "Use auto_suggest if you want window-scoped idle triggers.",
      0
    )
  end
  expect_exact_keys("watch.idle", merged.idle, { ms = true })
  expect_positive_number("watch.idle.ms", merged.idle.ms)
  expect_non_negative_number("watch.cooldown_ms", merged.cooldown_ms)
  return merged
end

function M.normalize_auto_suggest(raw, defaults)
  if raw == nil then
    return vim.deepcopy(defaults)
  end
  -- Master off switch — documented in the ADR as the way to disable
  -- auto_suggest entirely without removing the (future) defaults.
  if raw == false then
    return false
  end

  expect_table("auto_suggest", raw)
  expect_exact_keys("auto_suggest", raw, {
    idle = true,
    keys = true,
    cost = true,
    default_cost = true,
    cooldown_ms = true,
  })

  local merged = vim.tbl_deep_extend("force", vim.deepcopy(defaults), raw)
  local trigger_count = 0

  if merged.idle ~= nil then
    normalize_idle_trigger("auto_suggest.idle", merged.idle)
    trigger_count = trigger_count + 1
  end
  if merged.keys ~= nil then
    normalize_keys_trigger("auto_suggest.keys", merged.keys)
    trigger_count = trigger_count + 1
  end
  if merged.cost ~= nil then
    normalize_cost_trigger("auto_suggest.cost", merged.cost)
    trigger_count = trigger_count + 1
  end

  -- Fallback gate for on-demand `:Vimfy suggest on`; always present from
  -- defaults. Same shape as a cost trigger, but it is not a startup trigger,
  -- so it does not satisfy the requirement below.
  normalize_cost_trigger("auto_suggest.default_cost", merged.default_cost)

  if trigger_count == 0 and raw.default_cost == nil then
    error("auto_suggest must configure at least one trigger (idle/keys/cost). " ..
      "For on-demand use, run `:Vimfy suggest on` with no block; to tune that " ..
      "gate, set auto_suggest.default_cost.", 0)
  end

  expect_non_negative_number("auto_suggest.cooldown_ms", merged.cooldown_ms)
  return merged
end

return M
