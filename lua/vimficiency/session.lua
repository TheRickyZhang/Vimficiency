local v = vim.api
local cmd = vim.cmd

local util = require("vimficiency.util")
local types = require("vimficiency.types")
local simulate = require("vimficiency.simulate")

local M = {}


-- Start locals
local data_root = vim.fs.joinpath(vim.fn.stdpath("data"), "vimficiency")

local defaults = {
  exe = "vimficiency",           -- or your dev build path
  root = data_root,              -- base dir for plugin data
  start_dir = "StartingStates",  -- relative to root by default
  end_dir   = "EndingStates",    -- relative to root by default
}
local config = vim.deepcopy(defaults)

---@type VimficiencySession | nil
local session = nil
-- End locals


-------- Helper functions -------- 
-- Simple concatenation of string paths
local function join(...)
  return table.concat({ ... }, "/")
end

local function is_abs(path)
  return path:sub(1, 1) == "/"
end

local function file_exists(p)
  local st = vim.uv.fs_stat(p)
  return st and st.type == "file"
end



---@return VimficiencyWriteDTO
local function initializeAndWrite(dir)
  local buf = v.nvim_get_current_buf()
  local win = v.nvim_get_current_win()
  local id = util.new_id(buf)
  local path = join(dir, id .. ".json")

  ---@type VimficiencyFileContents
  local state = util.capture_state(buf, win)

  util.write_json(path, state)

  return types.new_write_dto(buf, win, id, path)
end
-------- Helper functions end -------- 


function M.setup(setup_options)
  config = vim.tbl_deep_extend("force", defaults, setup_options or {})

  -- If user gave relative dirs, anchor them under root.
  -- If they gave absolute dirs, use them as-is and ignore root.
  if not is_abs(config.start_dir) then
    config.start_dir = util.joinpath(config.root, config.start_dir)
  end
  if not is_abs(config.end_dir) then
    config.end_dir = util.joinpath(config.root, config.end_dir)
  end

  -- At this point: config.start_dir and config.end_dir are full paths.
  util.ensure_dir(config.start_dir)
  util.ensure_dir(config.end_dir)
  v.nvim_create_user_command("VimficiencyStart", function()
    ---@type VimficiencyWriteDTO
    local res = initializeAndWrite(config.start_dir)

    session = types.new_session(res.id, res.path, res.buf)

    vim.notify("vimficiency started " .. res.id, vim.log.levels.INFO)
  end, {})

  v.nvim_create_user_command("VimficiencyEnd", function()
    if not session then
      util.show_output("no session, ensure you called this in correct context")
      vim.notify("no session found", vim.log.levels.ERROR)
      return
    end

    ---@type VimficiencyWriteDTO
    local res = initializeAndWrite(config.end_dir)

    if res.buf ~= session.start_buf then
      util.show_output("not in same buffer (not currently implemented)")
      return
    end

    local code, stdout, stderr = util.run_program(config.exe, session.start_path, res.path)

    local id = session.id
    session = nil;

    if code ~= 0 then
      util.show_output("error " .. id .. "\n" .. (stderr ~= "" and stderr or stdout))
      vim.notify("vimficiency failed (exit " .. code .. ")", vim.log.levels.ERROR)
      return
    end

    util.show_output("vimficiency " .. id, stdout)
  end, {})


  v.nvim_create_user_command("VimficiencySimulate", function(opts)
    if not session then
      util.show_output("no session found")
      return
    end

    if not file_exists(session.start_path) then
      util.show_output("start path not found")
      return
    end

    ---@type VimficiencyFileContents | nil
    local data = util.read_json(session.start_path)
    if not data then
      return
    end

    local keys = opts.args
    simulate.simulate_visible(data.lines, data.row, data.col, keys, 500)
  end, {
      nargs = "1"
    })
end

return M
