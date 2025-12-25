-- return require("vimficiency.session")

local session = require("vimficiency.session")

local M = {}

local data_root = vim.fs.joinpath(vim.fn.stdpath("data"), "vimficiency")


---@type VimficiencyConfig
local config = {
  exe = "/home/ricky/Projects/vimficiency/build/vimficiency",  --- Full path for dev
  root = data_root,              -- base dir for plugin data
  start_dir = "StartingStates",  -- relative to root by default
  end_dir   = "EndingStates",    -- relative to root by default
}

-- Simple concatenation of string paths
local function join(...)
  return table.concat({ ... }, "/")
end

local function is_abs(path)
  return path:sub(1, 1) == "/"
end

local function normalize_and_ensure(cfg)
  if not is_abs(cfg.start_dir) then
    cfg.start_dir = join(cfg.root, cfg.start_dir)
  end
  if not is_abs(cfg.end_dir) then
    cfg.end_dir = join(cfg.root, cfg.end_dir)
  end
  vim.fn.mkdir(cfg.start_dir, "p")
  vim.fn.mkdir(cfg.end_dir, "p")
end

local function set_cmd(name, fn, opts)
  opts = opts or {}
  pcall(vim.api.nvim_del_user_command, name)
  vim.api.nvim_create_user_command(name, fn, opts)
end



function M.setup(setup_options)
  config = vim.tbl_deep_extend("force", config, setup_options or {})
  config.exe = vim.fn.expand(config.exe)

  normalize_and_ensure(config)
  session.setup(config)

  set_cmd("VimficiencyStart", session.start, {})
  set_cmd("VimficiencyStop", session.finish, {})

  set_cmd("VimficiencySimulate", function(o)
    session.simulate(o.args)
  end, {nargs = 1})

  set_cmd("VimficiencyReload", function()
    for name, _ in pairs(package.loaded) do
      if name:match("vimficiency") then
        package.loaded[name] = nil
      end
    end
    require("vimficiency").setup({})
    vim.notify("vimficiency reloaded", vim.log.levels.INFO)
  end, {})
end

return M
