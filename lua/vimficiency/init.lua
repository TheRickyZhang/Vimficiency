local config = require("vimficiency.config")
local key_tracking = require("vimficiency.capture.key_tracking")
local auto_suggest = require("vimficiency.capture.auto_suggest")
local commands = require("vimficiency.commands")
local mapping_scan = require("vimficiency.mapping_scan")
local recall_capture = require("vimficiency.capture.recall")
local ffi_config = require("vimficiency.ffi.config")

local M = {}

M.config = config

--- Named augroup for every autocmd the plugin installs.
--- Clearing on create keeps `setup()` idempotent.
M.AUGROUP = "Vimficiency"

local function set_cmd(name, fn, opts)
  opts = opts or {}
  pcall(vim.api.nvim_del_user_command, name)
  vim.api.nvim_create_user_command(name, fn, opts)
end

local function handle_vf_command(opts)
  local prev = key_tracking.begin_ignore()
  local ok, err = pcall(function()
    commands.handle(opts.args)
  end)
  key_tracking.end_ignore(prev)
  if not ok then
    vim.notify(tostring(err), vim.log.levels.ERROR)
  end
end

---@param fn fun(...): any
---@return fun(...): any
function M.wrap(fn)
  return function(...)
    local prev = key_tracking.begin_ignore()
    local ok, err = pcall(fn, ...)
    key_tracking.end_ignore(prev)
    if not ok then
      error(err)
    end
  end
end

---@param subcmd string
---@param subcmd_args string[]
---@param source string
---@return fun()
local function build_subcmd_callback(subcmd, subcmd_args, source)
  return M.wrap(function()
    commands.run(subcmd, subcmd_args, source)
  end)
end

---@param name string
---@param subcmd string
---@param subcmd_args string[]
local function register_plug(name, subcmd, subcmd_args)
  vim.keymap.set("n", "<Plug>Vimfy" .. name,
    build_subcmd_callback(subcmd, subcmd_args, "<Plug>Vimfy" .. name),
    { silent = true, desc = "Vimfy " .. subcmd .. " " .. table.concat(subcmd_args, " ") })
end

---@param mode string|string[]
---@param lhs string
---@param spec string|fun(): any
---@param opts table|nil
function M.map(mode, lhs, spec, opts)
  opts = opts or {}
  if opts.silent == nil then opts.silent = true end

  local callback
  if type(spec) == "string" then
    local parts = vim.split(spec, "%s+", { trimempty = true })
    if #parts == 0 then
      error("vimfy.map: empty spec string")
    end
    local subcmd = parts[1]
    local subcmd_args = {}
    for i = 2, #parts do subcmd_args[#subcmd_args + 1] = parts[i] end
    callback = build_subcmd_callback(subcmd, subcmd_args, "vimfy.map(" .. lhs .. ")")
    if not opts.desc then opts.desc = "Vimfy " .. spec end
  elseif type(spec) == "function" then
    callback = M.wrap(spec)
    if not opts.desc then opts.desc = "Vimfy <fn>" end
  else
    error("vimfy.map: spec must be a string or function, got " .. type(spec))
  end

  vim.keymap.set(mode, lhs, callback, opts)
end

function M.setup(user_config)
  user_config = user_config or {}
  config.reset()

  -- Stash config so reload can re-run `setup()` with the same values.
  _G.__vimficiency_last_user_config = user_config

  vim.api.nvim_create_augroup(M.AUGROUP, { clear = true })

  local plugin_dir = debug.getinfo(1, "S").source:sub(2):match("(.*)/lua/")
  vim.cmd.helptags(plugin_dir .. "/doc")

  if not user_config.shiftwidth then
    user_config.shiftwidth = vim.o.shiftwidth
  end

  local lua_consumed = config.apply(user_config)
  local cpp_consumed = ffi_config.configure(user_config)

  local unknown = {}
  for key in pairs(user_config) do
    if not lua_consumed[key] and not cpp_consumed[key] then
      table.insert(unknown, tostring(key))
    end
  end
  if #unknown > 0 then
    table.sort(unknown)
    vim.notify(
      "vimfy: unknown config keys ignored: " .. table.concat(unknown, ", "),
      vim.log.levels.WARN
    )
  end

  set_cmd("Vimfy", handle_vf_command, {
    nargs = "*",
    complete = commands.complete,
    desc = "Vimfy movement optimizer",
  })

  set_cmd("Vimficiency", handle_vf_command, {
    nargs = "*",
    complete = commands.complete,
    desc = "Vimfy movement optimizer",
  })

  for _, alias in ipairs({ "a", "b", "c", "d", "e" }) do
    local upper = alias:upper()
    register_plug("Start" .. upper, "start", { alias })
    register_plug("Watch" .. upper, "watch", { alias })
    register_plug("Finish" .. upper, "finish", { alias })
    register_plug("Close" .. upper, "close", { alias })
    register_plug("Play" .. upper, "play", { alias })
  end
  register_plug("SuggestOn", "suggest", { "on" })
  register_plug("SuggestOff", "suggest", { "off" })
  register_plug("SuggestToggle", "suggest", { "toggle" })
  register_plug("List", "list", {})
  register_plug("Config", "config", {})
  register_plug("Help", "help", {})

  recall_capture.install()

  if config.auto_suggest and auto_suggest.is_configured() then
    auto_suggest.enable()
  end

  mapping_scan.warn_about_bad_mappings()
end

--- Tear down runtime state that should not outlive a Lua reload.
---@return { dropped_active: integer, sim_closed: boolean }
function M.shutdown()
  local simulate = require("vimficiency.simulate")
  local session_store = require("vimficiency.session.store")

  -- Close any open replay tab.
  local sim_closed = false
  local ok = pcall(simulate.cleanup_compare)
  if ok then sim_closed = true end

  pcall(auto_suggest.disable)

  -- Drop active captures; finished records stay.
  local dropped_active = 0
  local ok_drop, n = pcall(session_store.teardown_active)
  if ok_drop then dropped_active = n or 0 end

  -- Detach the shared global `vim.on_key` namespace last.
  pcall(key_tracking.shutdown)

  pcall(vim.api.nvim_create_augroup, M.AUGROUP, { clear = true })

  return { dropped_active = dropped_active, sim_closed = sim_closed }
end

--- Reload every `vimficiency.*` Lua module, then re-run `setup()`.
--- Finished session records survive; active captures do not.
---@param new_lib_path string|nil  Path to a freshly built .so; nil = keep
---                                the currently cached library.
---@return { dropped_active: integer, preserved_records: integer }
function M.reload_lua(new_lib_path)
  local session_store = require("vimficiency.session.store")

  -- Snapshot state before teardown.
  local status = M.shutdown()
  local dump = session_store.dump_for_reload()
  local preserved_records = 0
  for _ in pairs(dump.records) do preserved_records = preserved_records + 1 end

  if new_lib_path and new_lib_path ~= "" then
    _G.__vimficiency_reload_lib_path = new_lib_path
  end
  for name in pairs(package.loaded) do
    if name == "vimficiency" or name:sub(1, 12) == "vimficiency." then
      package.loaded[name] = nil
    end
  end

  local fresh = require("vimficiency")
  fresh.setup(_G.__vimficiency_last_user_config or {})

  require("vimficiency.session.store").restore_from_dump(dump)

  return {
    dropped_active    = status.dropped_active,
    preserved_records = preserved_records,
  }
end

return M
