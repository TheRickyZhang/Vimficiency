local alias_mod = require("vimficiency.session.alias")
local disk = require("vimficiency.session.disk")
local session_store = require("vimficiency.session.store")
local simulate = require("vimficiency.simulate")

local M = {}

---@param result VF.Session.Result
---@param label string
---@param count integer|nil
local function run_compare(result, label, count)
  local function fmt_cost(c)
    return c and string.format("%.2f", c) or nil
  end

  local optimal_results = result.optimal_results or {}
  local user_seq = result.user_seq or ""
  local first_optimal = optimal_results[1] and optimal_results[1].seq or ""

  local user_item = nil
  if user_seq ~= "" and user_seq ~= first_optimal then
    user_item = { seq = user_seq, cost = fmt_cost(result.user_cost) }
  end

  local suggestions = {}
  for _, r in ipairs(optimal_results) do
    table.insert(suggestions, { seq = r.seq, cost = fmt_cost(r.cost) })
  end

  if user_item == nil and #suggestions == 0 then
    vim.notify("No sequences to simulate", vim.log.levels.WARN)
    return
  end

  simulate.simulate_compare(
    result.lines,
    result.start_row,
    result.start_col,
    {
      user = user_item,
      suggestions = suggestions,
    },
    {
      label = label,
      end_row = result.end_row,
      end_col = result.end_col,
      initial_window_count = count,
    }
  )
end

---@param alias string
---@param count integer|nil
function M.simulate(alias, count)
  if not alias or alias == "" then
    vim.notify("simulate() requires a session alias or saved name", vim.log.levels.ERROR)
    return
  end

  -- `@` = most recently finished session; `$` = most recent suggest regression.
  local _, special_result, is_special = session_store.resolve_special(alias)
  if is_special then
    if not special_result then
      local what = alias == "$" and "suggest regression" or "finished session"
      vim.notify("No recent " .. what .. " to replay.", vim.log.levels.ERROR)
      return
    end
    run_compare(special_result, alias, count)
    return
  end

  local in_memory = session_store.get_result(alias)
  local on_disk = nil
  if alias_mod.is_valid_saved_name(alias) then
    local data, err, is_missing = disk.load(alias)
    if data then
      on_disk = data
    elseif not is_missing then
      vim.notify("simulate: " .. (err or "unknown error"), vim.log.levels.ERROR)
      return
    end
  end

  local result
  if in_memory and on_disk then
    vim.notify(
      "'" .. alias .. "' exists in both session memory and on disk — " ..
      "replaying the in-memory copy. (The disk copy is untouched; " ..
      "`:Vimfy fetch " .. alias .. " <other-alias>` to inspect it " ..
      "separately, or `:Vimfy store " .. alias .. " <new-name>` to move " ..
      "the in-memory copy aside so future sims see the disk copy.)",
      vim.log.levels.WARN)
    result = in_memory
  elseif in_memory then
    result = in_memory
  elseif on_disk then
    if alias_mod.is_valid_manual(alias) then
      local id, reg_err = session_store.register_fetched_result(alias, on_disk)
      if id then
        vim.notify("vimfy: fetched [" .. alias .. "] into session", vim.log.levels.INFO)
      else
        vim.notify("vimfy: replaying '" .. alias .. "' directly from disk " ..
          "(implicit fetch failed: " .. (reg_err or "unknown error") ..
          "). Use `:Vimfy fetch " .. alias .. " <alias>` to keep it in the workspace.",
          vim.log.levels.WARN)
      end
    end
    result = on_disk
  else
    vim.notify("No results for '" .. alias .. "' in session or on disk.",
      vim.log.levels.ERROR)
    return
  end

  local function fmt_cost(c)
    return c and string.format("%.2f", c) or nil
  end

  local optimal_results = result.optimal_results or {}
  local user_seq = result.user_seq or ""
  local first_optimal = optimal_results[1] and optimal_results[1].seq or ""

  local user_item = nil
  if user_seq ~= "" and user_seq ~= first_optimal then
    user_item = { seq = user_seq, cost = fmt_cost(result.user_cost) }
  end

  local suggestions = {}
  for _, r in ipairs(optimal_results) do
    table.insert(suggestions, { seq = r.seq, cost = fmt_cost(r.cost) })
  end

  if user_item == nil and #suggestions == 0 then
    vim.notify("No sequences to simulate", vim.log.levels.WARN)
    return
  end

  simulate.simulate_compare(
    result.lines,
    result.start_row,
    result.start_col,
    {
      user = user_item,
      suggestions = suggestions,
    },
    {
      label = alias,
      end_row = result.end_row,
      end_col = result.end_col,
      initial_window_count = count,
    }
  )
end

return M
