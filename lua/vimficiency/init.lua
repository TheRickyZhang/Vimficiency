local config = require("vimficiency.config")
local key_tracking = require("vimficiency.capture.key_tracking")
local auto_suggest = require("vimficiency.capture.auto_suggest")
local commands = require("vimficiency.commands")
local mapping_scan = require("vimficiency.mapping_scan")
local recall_capture = require("vimficiency.capture.recall")
local ffi_lib = require("vimficiency.ffi")

local M = {}

M.config = config

--- Named augroup for every autocmd the plugin installs. Cleared-on-create
--- makes setup() idempotent: re-running it (via `:Vimfy reload`) wipes any
--- autocmds the previous load registered under this group.
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
    { silent = true, desc = "Vimficiency " .. subcmd .. " " .. table.concat(subcmd_args, " ") })
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
      error("vimficiency.map: empty spec string")
    end
    local subcmd = parts[1]
    local subcmd_args = {}
    for i = 2, #parts do subcmd_args[#subcmd_args + 1] = parts[i] end
    callback = build_subcmd_callback(subcmd, subcmd_args, "vimfy.map(" .. lhs .. ")")
    if not opts.desc then opts.desc = "Vimficiency " .. spec end
  elseif type(spec) == "function" then
    callback = M.wrap(spec)
    if not opts.desc then opts.desc = "Vimficiency <fn>" end
  else
    error("vimficiency.map: spec must be a string or function, got " .. type(spec))
  end

  vim.keymap.set(mode, lhs, callback, opts)
end

function M.setup(user_config)
  user_config = user_config or {}
  config.reset()

  -- Stashed on a global so the reload path can re-invoke `setup()` with the
  -- same configuration after niling `package.loaded[...]`. Survives the
  -- module wipe by virtue of living on `_G`.
  _G.__vimficiency_last_user_config = user_config

  -- Idempotent autocmd ownership: clearing the group on every setup means a
  -- reload doesn't accumulate ghost autocmds.
  vim.api.nvim_create_augroup(M.AUGROUP, { clear = true })

  local plugin_dir = debug.getinfo(1, "S").source:sub(2):match("(.*)/lua/")
  vim.cmd.helptags(plugin_dir .. "/doc")

  if not user_config.shiftwidth then
    user_config.shiftwidth = vim.o.shiftwidth
  end

  local lua_consumed = config.apply(user_config)
  local cpp_consumed = ffi_lib.configure(user_config)

  local unknown = {}
  for key in pairs(user_config) do
    if not lua_consumed[key] and not cpp_consumed[key] then
      table.insert(unknown, tostring(key))
    end
  end
  if #unknown > 0 then
    table.sort(unknown)
    vim.notify(
      "vimficiency: unknown config keys ignored: " .. table.concat(unknown, ", "),
      vim.log.levels.WARN
    )
  end

  set_cmd("Vimfy", handle_vf_command, {
    nargs = "*",
    complete = commands.complete,
    desc = "Vimficiency motion optimizer",
  })

  set_cmd("Vimficiency", handle_vf_command, {
    nargs = "*",
    complete = commands.complete,
    desc = "Vimficiency motion optimizer",
  })

  for _, alias in ipairs({ "a", "b", "c", "d", "e" }) do
    local upper = alias:upper()
    register_plug("Start" .. upper, "start", { alias })
    register_plug("Watch" .. upper, "watch", { alias })
    register_plug("End" .. upper, "end", { alias })
    register_plug("Close" .. upper, "close", { alias })
    register_plug("Sim" .. upper, "sim", { alias })
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

--- Tear down every piece of runtime state this plugin installs that would
--- otherwise outlive a `package.loaded[...]` wipe (autocmds, vim.on_key
--- callbacks, sim-tab windows, active capture sessions, etc.). Leaves
--- finished session records in place so the reloader can carry them over.
---
--- Returns a small status table the reloader surfaces to the user.
---@return { dropped_active: integer, sim_closed: boolean }
function M.shutdown()
  local simulate = require("vimficiency.simulate")
  local session_store = require("vimficiency.session.store")

  -- Close any open replay tab (buffer-local keymaps / extmarks reference
  -- closures from this module and would otherwise linger as a ghost UI).
  local sim_closed = false
  local ok = pcall(simulate.cleanup_compare)
  if ok then sim_closed = true end

  -- Disable auto-suggest (disarms every armed end_trigger, which in turn
  -- detaches their `attach_global` subscribers and closes their timers).
  pcall(auto_suggest.disable)

  -- Drop active captures: detaches per-session `vim.on_key` callbacks and
  -- disarms watch triggers. Finished records (with results) stay.
  local dropped_active = 0
  local ok_drop, n = pcall(session_store.teardown_active)
  if ok_drop then dropped_active = n or 0 end

  -- Detach the shared global `vim.on_key` namespace. Must come AFTER
  -- session/auto_suggest teardown, since those call `detach_global` and
  -- we want a clean `global_subs` afterwards.
  pcall(key_tracking.shutdown)

  -- Clear our augroup. Setup() will recreate it; this makes shutdown
  -- correct even when called outside the reload flow.
  pcall(vim.api.nvim_create_augroup, M.AUGROUP, { clear = true })

  return { dropped_active = dropped_active, sim_closed = sim_closed }
end

--- Reload every Lua module under `vimficiency.*`, then re-run setup with
--- the last-known user config. Optionally accepts a path to a freshly
--- built `libvimficiency.so` — if provided, the new ffi module will load
--- from that path, bypassing dlopen's per-path caching so C++ changes
--- take effect without restarting Neovim.
---
--- Finished session records (the ones reachable via `:Vimfy sim`, `:Vimfy
--- list`, etc.) survive the reload; active captures do not.
---
---@param new_lib_path string|nil  Path to a freshly built .so; nil = keep
---                                the currently cached library.
---@return { dropped_active: integer, preserved_records: integer }
function M.reload_lua(new_lib_path)
  local session_store = require("vimficiency.session.store")

  -- Phase 1: snapshot what we want to carry across the reload. Must come
  -- before teardown so the active-session destruction in `shutdown()`
  -- doesn't race with the dump.
  local status = M.shutdown()
  local dump = session_store.dump_for_reload()
  local preserved_records = 0
  for _ in pairs(dump.records) do preserved_records = preserved_records + 1 end

  -- Phase 2: point the next ffi.load at the new .so (if any), then wipe
  -- every `vimficiency.*` module from package.loaded. The next require()
  -- will pull fresh source from disk.
  if new_lib_path and new_lib_path ~= "" then
    _G.__vimficiency_reload_lib_path = new_lib_path
  end
  for name in pairs(package.loaded) do
    if name == "vimficiency" or name:sub(1, 12) == "vimficiency." then
      package.loaded[name] = nil
    end
  end

  -- Phase 3: re-require the top-level module and re-run setup with the
  -- user's original config. This re-registers user commands, <Plug>
  -- mappings, recall_capture, auto_suggest (if configured), and clears
  -- the augroup back to empty.
  local fresh = require("vimficiency")
  fresh.setup(_G.__vimficiency_last_user_config or {})

  -- Phase 4: rehydrate finished session records into the fresh store.
  require("vimficiency.session.store").restore_from_dump(dump)

  return {
    dropped_active    = status.dropped_active,
    preserved_records = preserved_records,
  }
end

return M
