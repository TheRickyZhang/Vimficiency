local v = vim.api

local alias_mod = require("vimficiency.session.alias")
local config = require("vimficiency.config")
local compute = require("vimficiency.session.compute")
local disk = require("vimficiency.session.disk")
local end_trigger = require("vimficiency.capture.end_trigger")
local key_tracking = require("vimficiency.capture.key_tracking")
local playback = require("vimficiency.session.playback")
local result_view = require("vimficiency.session.result_view")
local saved_view = require("vimficiency.session.saved_view")
local session_store = require("vimficiency.session.store")
local util = require("vimficiency.util")

local M = {}

M.empty_array = disk.empty_array
M.compute_result_for_active = compute.compute_result_for_active
M.manual_should_evict = compute.manual_should_evict

---@param record VF.Session.Record
---@param title string
---@param text string
---@param notify_message string|nil
---@param level integer|nil
local function total_failure(record, title, text, notify_message, level)
  util.show_output(title, text, {
    ui_keys = {
      title = "Vimfy Scratch Output Keys",
      docs = true,
    },
  })
  if record.start_kind == "manual" then
    session_store.remove(record.id)
  end
  if notify_message or title then
    vim.schedule(function()
      vim.notify(notify_message or title, level or vim.log.levels.ERROR)
    end)
  end
end

---@param alias string
---@return boolean
local function validate_manual_alias(alias)
  if not alias or alias == "" then
    return false
  end
  if not alias_mod.is_valid_manual(alias) then
    vim.notify(
      "Invalid session alias '" .. alias .. "'. " ..
      "Manual aliases are alphabetic only (e.g. 'a', 'refactor').",
      vim.log.levels.ERROR
    )
    return false
  end
  return true
end

---@param alias string
---@param end_kind VF.Session.EndKind
---@return VF.Session.Active
local function start_manual_session(alias, end_kind)
  local buf = v.nvim_get_current_buf()
  local win = v.nvim_get_current_win()
  local id = util.new_id(buf)
  local start_state = util.capture_state(buf, win)

  local key_nsid = key_tracking.attach(
    function()
      return session_store.get_active(alias)
    end,
    function(reason, level)
      session_store.remove(id)
      if reason then
        vim.schedule(function()
          vim.notify(reason, level or vim.log.levels.INFO)
        end)
      end
    end,
    function(session)
      if not v.nvim_win_is_valid(session.win) then
        local noun = end_kind == "auto" and "watch" or "session"
        return "Vimfy: " .. noun .. " [" .. alias .. "] dropped — window closed"
      end
      local cursor = v.nvim_win_get_cursor(session.win)
      local reason = compute.manual_should_evict(session, cursor[1] - 1, vim.uv.hrtime())
      if reason then
        local noun = end_kind == "auto" and "watch" or "session"
        return "Vimfy: " .. noun .. " [" .. alias .. "] dropped — " .. reason
      end
      return nil
    end
  )

  return session_store.new_active_session(
    id, key_nsid, win, buf, start_state, "manual", end_kind)
end

---@param alias string
function M.start(alias)
  if not alias or alias == "" then
    vim.notify("start() requires a session alias", vim.log.levels.ERROR)
    return
  end
  if not validate_manual_alias(alias) then return end
  if not session_store.can_store_manual(alias) then
    vim.notify("Manual session capacity reached", vim.log.levels.ERROR)
    return
  end

  local active = start_manual_session(alias, "manual")
  local overwrote = session_store.store_manual(alias, active)

  if overwrote then
    vim.notify("vimfy started [" .. alias .. "] (overwrote existing)", vim.log.levels.INFO)
  else
    vim.notify("vimfy started [" .. alias .. "]", vim.log.levels.INFO)
  end
end

---@param alias string
function M.watch(alias)
  if not alias or alias == "" then
    vim.notify("watch() requires a session alias", vim.log.levels.ERROR)
    return
  end
  if not validate_manual_alias(alias) then return end

  local cfg = config.watch
  if not cfg or not cfg.idle then
    vim.notify(
      "Watch is not configured. Add `watch = { idle = { ms = N }, cooldown_ms = N }` to setup{}.",
      vim.log.levels.ERROR
    )
    return
  end

  if not session_store.can_store_manual(alias) then
    vim.notify("Manual session capacity reached", vim.log.levels.ERROR)
    return
  end

  local active = start_manual_session(alias, "auto")
  local id = active.id

  local disarm = end_trigger.arm_idle({
    name = "watch_" .. id,
    idle_ms = cfg.idle.ms,
    cooldown_ms = cfg.cooldown_ms,
    on_fire = function()
      local rec = session_store.get_active(alias)
      if not rec or rec.id ~= id then return false end
      M.finish(alias, "watch_idle")
      return true
    end,
  })

  if not disarm then
    key_tracking.detach(active.key_nsid)
    vim.notify("vimfy watch [" .. alias .. "] failed to arm idle trigger", vim.log.levels.ERROR)
    return
  end

  active.watch_disarm = disarm
  local overwrote = session_store.store_manual(alias, active)

  if overwrote then
    vim.notify("vimfy watching [" .. alias .. "] (overwrote existing)", vim.log.levels.INFO)
  else
    vim.notify("vimfy watching [" .. alias .. "]", vim.log.levels.INFO)
  end
end

---@param active VF.Session.Active
---@param alias string
---@param reason VF.Session.FinishReason
local function do_finish(active, alias, reason)
  local id = active.id

  local result, err = compute.compute_result_for_active(active)
  if not result then
    total_failure(active, "finish()", err or "unknown error")
    return
  end

  if not session_store.finish_session(id, result, alias, nil, reason) then
    total_failure(active, "finish()", "failed to store result")
    return
  end

  vim.notify(
    result_view.format_message("vimfy finished [" .. alias .. "]", result),
    vim.log.levels.INFO)
end

---@param alias string
---@param reason VF.Session.FinishReason|nil
function M.finish(alias, reason)
  reason = reason or "manual"
  if not alias or alias == "" then
    vim.notify("finish() requires a session alias", vim.log.levels.ERROR)
    return
  end

  local session_type = alias_mod.parse(alias)
  if session_type == "recall_key" or session_type == "recall_time" then
    vim.notify(
      "`:Vimfy finish " .. alias .. "` is not a manual handle. " ..
      "Use `:Vimfy recall " .. alias .. "` for retrospective windows.",
      vim.log.levels.ERROR)
    return
  end
  if session_type ~= "manual" then
    vim.notify(
      "Invalid session alias '" .. alias .. "'. " ..
      "Manual aliases are alphabetic only (e.g. 'a', 'refactor').",
      vim.log.levels.ERROR)
    return
  end

  local active = session_store.get_active(alias)
  if not active then
    vim.notify("Session '" .. alias .. "' not found or already finished.", vim.log.levels.ERROR)
    return
  end

  do_finish(active, alias, reason)
end

---@param alias string
function M.recall(alias)
  if not alias or alias == "" then
    vim.notify(
      "recall() requires a window alias (e.g. '5' for keys ago, '3s' for seconds)",
      vim.log.levels.ERROR)
    return
  end

  local session_type = alias_mod.parse(alias)
  if session_type == "manual" then
    vim.notify(
      "`:Vimfy recall " .. alias .. "` expects a recall window (N or Ns). " ..
      "Use `:Vimfy finish " .. alias .. "` to finish a manual session.",
      vim.log.levels.ERROR)
    return
  end
  if session_type ~= "recall_key" and session_type ~= "recall_time" then
    vim.notify(
      "Invalid recall alias '" .. alias .. "'. " ..
      "Expected N (keys ago) or Ns (seconds ago).",
      vim.log.levels.ERROR)
    return
  end

  local active = session_store.get_active(alias)
  if not active then
    if session_type == "recall_key" then
      vim.notify("No recall session found for '" .. alias .. "' keys ago.", vim.log.levels.ERROR)
    else
      vim.notify(
        "No recall session found within '" .. alias .. "'. " ..
        "Try a larger window or indexing by key count (e.g. 'recall 20').",
        vim.log.levels.ERROR)
    end
    return
  end

  do_finish(active, alias, "manual")
end

---@param selector string
---@return string|nil
function M.default_save_name(selector)
  if selector == "@" then
    return session_store.get_last_finished_alias()
  end
  return selector
end

---@param selector string
---@return VF.Session.Result|nil result
---@return string|nil err
local function resolve_result_for_selector(selector)
  if selector == "@" then
    local result = session_store.get_last_finished_result()
    if not result then
      return nil, "No recently finished session. Run ':Vimfy finish <alias>' first."
    end
    return result, nil
  end
  local result = session_store.get_result(selector)
  if not result then
    return nil, "No finished result for '" .. selector .. "'. Is the session still active?"
  end
  return result, nil
end

---@param selector string
---@return VF.Session.Result|nil result
---@return string|nil err
function M.resolve_result(selector)
  local result, err = resolve_result_for_selector(selector)
  if result then return result, nil end

  if selector ~= "@" and alias_mod.is_valid_saved_name(selector) then
    local data, load_err, is_missing = disk.load(selector)
    if data then return data, nil end
    if not is_missing then
      return nil, load_err or "unknown error"
    end
  end

  return nil, err
end

---@param selector string
---@param name string
function M.save(selector, name)
  if not alias_mod.is_valid_saved_name(name) then
    vim.notify(
      "Invalid saved name '" .. tostring(name) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.",
      vim.log.levels.ERROR
    )
    return
  end

  local result, err = resolve_result_for_selector(selector)
  if not result then
    vim.notify(err or "unknown error", vim.log.levels.ERROR)
    return
  end

  local path, write_err = disk.write_with_overwrite_warn(name, result)
  if path then
    local display_path = vim.fn.fnamemodify(path, ":~")
    vim.notify("vimfy saved [" .. name .. "] → " .. display_path, vim.log.levels.INFO)
  else
    vim.notify("vimfy save failed: " .. (write_err or "unknown error"), vim.log.levels.ERROR)
  end
end

---@param selector string
---@param name string
function M.store(selector, name)
  if not alias_mod.is_valid_saved_name(name) then
    vim.notify(
      "Invalid saved name '" .. tostring(name) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.",
      vim.log.levels.ERROR
    )
    return
  end

  if session_store.get_active(selector) then
    local kind = alias_mod.parse(selector)
    local resolve_cmd = (kind == "recall_key" or kind == "recall_time")
      and (":Vimfy recall " .. selector)
      or (":Vimfy finish " .. selector)
    vim.notify("Session '" .. selector .. "' is still active. Finish it with '" ..
      resolve_cmd .. "' first.", vim.log.levels.ERROR)
    return
  end

  local id
  if selector == "@" then
    id = session_store.get_last_finished_id()
  else
    id = session_store.get_id(selector)
  end
  if not id then
    vim.notify("No finished result for '" .. selector ..
      "'. Is the session still active?", vim.log.levels.ERROR)
    return
  end

  local result = session_store.get_result_by_id(id)
  if not result then
    vim.notify("No finished result for '" .. selector .. "'.", vim.log.levels.ERROR)
    return
  end

  local path, write_err = disk.write_with_overwrite_warn(name, result)
  if not path then
    vim.notify("vimfy store failed: " .. (write_err or "unknown error"),
      vim.log.levels.ERROR)
    return
  end

  session_store.remove(id)
  local display_path = vim.fn.fnamemodify(path, ":~")
  vim.notify("vimfy stored [" .. selector .. "] → [" .. name .. "] at " ..
    display_path .. " (removed from session)", vim.log.levels.INFO)
end

---@param name string
---@param alias string
function M.fetch(name, alias)
  if not alias_mod.is_valid_saved_name(name) then
    vim.notify("Invalid saved name '" .. tostring(name) .. "'.", vim.log.levels.ERROR)
    return
  end
  if not alias_mod.is_valid_manual(alias) then
    vim.notify(
      "Invalid alias '" .. tostring(alias) ..
      "'. Manual aliases must be alphabetic only (e.g. `foo`).",
      vim.log.levels.ERROR
    )
    return
  end

  local data, err = disk.load(name)
  if not data then
    vim.notify("vimfy fetch failed: " .. (err or "unknown error"), vim.log.levels.ERROR)
    return
  end

  local id, reg_err = session_store.register_fetched_result(alias, data)
  if not id then
    vim.notify("vimfy fetch failed: " .. (reg_err or "unknown error"), vim.log.levels.ERROR)
    return
  end

  vim.notify("vimfy fetched [" .. name .. "] → [" .. alias .. "]", vim.log.levels.INFO)
end

---@param name string
function M.rm(name)
  if not alias_mod.is_valid_saved_name(name) then
    vim.notify(
      "Invalid saved name '" .. tostring(name) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.",
      vim.log.levels.ERROR
    )
    return
  end

  local ok, path_or_err = disk.remove(name)
  if ok then
    vim.notify("vimfy removed [" .. name .. "] ← " .. path_or_err, vim.log.levels.INFO)
  else
    vim.notify(path_or_err or "vimfy rm failed", vim.log.levels.ERROR)
  end
end

---@param old_alias string
---@param new_alias string
---@return boolean ok
---@return string|nil err
function M.rename_active(old_alias, new_alias)
  if not alias_mod.is_valid_manual(old_alias) then
    return false, "source alias must be a manual (alphabetic) alias"
  end
  return session_store.rename_manual_alias(old_alias, new_alias)
end

---@param src_alias string
---@param dst_alias string
---@return boolean ok
---@return string|nil err
function M.duplicate_active(src_alias, dst_alias)
  if not alias_mod.is_valid_manual(dst_alias) then
    return false, "target alias must be a manual (alphabetic) alias"
  end
  local result = session_store.get_result(src_alias)
  if not result then
    return false, "no finished result for '" .. tostring(src_alias) .. "'"
  end
  local _, reg_err = session_store.register_fetched_result(dst_alias, result)
  if reg_err then return false, reg_err end
  return true, nil
end

M.rename_saved = disk.rename
M.duplicate_saved = disk.duplicate

---@param alias string
function M.close(alias)
  if not alias or alias == "" then
    vim.notify("close() requires a session alias", vim.log.levels.ERROR)
    return
  end

  local active = session_store.get_active(alias)
  if not active then
    vim.notify("Session '" .. alias .. "' not found or already closed", vim.log.levels.WARN)
    return
  end

  session_store.remove(active.id)
  vim.notify("vimfy closed [" .. alias .. "]", vim.log.levels.INFO)
end

M.simulate = playback.simulate

---@return string[]
function M.list()
  return session_store.list_aliases()
end

---@return string[]
function M.list_saved()
  return disk.list()
end

---@param name string
function M.view(name)
  saved_view.open(name)
end

return M
