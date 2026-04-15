local ffi_lib = require("vimficiency.ffi")
local session = require("vimficiency.session")
local auto_suggest = require("vimficiency.auto_suggest")
local alias_mod = require("vimficiency.alias")
local util = require("vimficiency.util")

local M = {}

local subcommands = {}

subcommands.start = {
  desc = "Start a manual session",
  usage = "start <alias>",
  fn = function(args)
    local alias = args[1]
    if not alias or alias == "" then
      vim.notify("Usage: Vimfy start <alias>", vim.log.levels.ERROR)
      return
    end
    session.start(alias)
  end,
}

subcommands.watch = {
  desc = "Start a watch session (manual start, auto end on idle)",
  usage = "watch <alias>",
  fn = function(args)
    local alias = args[1]
    if not alias or alias == "" then
      vim.notify("Usage: Vimfy watch <alias>", vim.log.levels.ERROR)
      return
    end
    session.watch(alias)
  end,
}

subcommands["end"] = {
  desc = "Finish a manual session and show results",
  usage = "end <alias>",
  fn = function(args)
    local alias = args[1]
    if not alias or alias == "" then
      vim.notify("Usage: Vimfy end <alias>", vim.log.levels.ERROR)
      return
    end
    session.finish(alias)
  end,
}

subcommands.recall = {
  desc = "Finish a retrospective recall window (N keys ago or Ns)",
  usage = "recall <N|Ns>",
  fn = function(args)
    local alias = args[1]
    if not alias or alias == "" then
      vim.notify("Usage: Vimfy recall <N|Ns> (e.g. 5 or 3s)", vim.log.levels.ERROR)
      return
    end
    session.recall(alias)
  end,
}

subcommands.close = {
  desc = "Close a session without finishing",
  usage = "close <alias>",
  fn = function(args)
    local alias = args[1]
    if not alias or alias == "" then
      vim.notify("Usage: Vimfy close <alias>", vim.log.levels.ERROR)
      return
    end
    session.close(alias)
  end,
}

subcommands.sim = {
  desc = "Simulate motion sequences",
  usage = "sim <alias> [count] [delay_ms]",
  fn = function(args)
    local alias = args[1]
    local count = args[2] and tonumber(args[2]) or nil
    local delay_ms = args[3] and tonumber(args[3]) or nil
    if not alias or alias == "" then
      vim.notify("Usage: Vimfy sim <alias> [count] [delay_ms]", vim.log.levels.ERROR)
      return
    end
    session.simulate(alias, count, delay_ms)
  end,
}

subcommands.save = {
  desc = "Save a finished session result to disk",
  usage = "save <selector>|@ [<name>]",
  fn = function(args)
    local selector = args[1]
    if not selector or selector == "" then
      vim.notify("Usage: Vimfy save <selector>|@ [<name>]", vim.log.levels.ERROR)
      return
    end
    local name = args[2]
    if not name or name == "" then
      name = session.default_save_name(selector)
      if not name then
        vim.notify(
          "Vimfy save: cannot derive a default name from '" .. selector ..
          "'. Supply one explicitly (e.g. `:Vimfy save " .. selector .. " my-name`).",
          vim.log.levels.ERROR)
        return
      end
    end
    session.save(selector, name)
  end,
}

subcommands.view = {
  desc = "View saved results",
  usage = "view [name]",
  fn = function(args)
    session.view(args[1])
  end,
}

subcommands.rm = {
  desc = "Delete a saved result from disk",
  usage = "rm <name>",
  fn = function(args)
    local name = args[1]
    if not name or name == "" then
      vim.notify("Usage: Vimfy rm <name>", vim.log.levels.ERROR)
      return
    end
    session.rm(name)
  end,
}

subcommands.list = {
  desc = "List active sessions and saved files",
  usage = "list",
  fn = function()
    local aliases = session.list()
    local saved = session.list_saved()
    local lines = {}
    if #aliases > 0 then
      table.insert(lines, "Active sessions: " .. table.concat(aliases, ", "))
    else
      table.insert(lines, "Active sessions: (none)")
    end
    if #saved > 0 then
      table.insert(lines, "Saved results: " .. table.concat(saved, ", "))
    else
      table.insert(lines, "Saved results: (none)")
    end
    vim.notify(table.concat(lines, "\n"), vim.log.levels.INFO)
  end,
}

subcommands.suggest = {
  desc = "Control auto-suggest (idle trigger)",
  usage = "suggest <on|off|toggle>",
  fn = function(args)
    local action = args[1]
    local function enable_or_warn()
      if not auto_suggest.is_configured() then
        vim.notify(
          "auto_suggest has no triggers configured. Add `auto_suggest = { idle = { ms = N, window = 'Ns' } }` or another full trigger to setup{}.",
          vim.log.levels.ERROR
        )
        return
      end
      if auto_suggest.enable() then
        vim.notify("vimficiency auto-suggest enabled", vim.log.levels.INFO)
      else
        vim.notify("vimficiency auto-suggest already enabled", vim.log.levels.WARN)
      end
    end

    if action == "on" then
      enable_or_warn()
    elseif action == "off" then
      auto_suggest.disable()
      vim.notify("vimficiency auto-suggest disabled", vim.log.levels.INFO)
    elseif action == "toggle" then
      if auto_suggest.is_enabled() then
        auto_suggest.disable()
        vim.notify("vimficiency auto-suggest disabled", vim.log.levels.INFO)
      else
        enable_or_warn()
      end
    else
      vim.notify("Usage: Vimfy suggest <on|off|toggle>", vim.log.levels.ERROR)
    end
  end,
}

local reload_in_progress = false

local function parse_build_progress(line)
  local m = line:match("^(%[%s*%d+/%d+%])")
  if m then return m end
  m = line:match("^(%[%s*%d+%%%])")
  if m then return m end
  return nil
end

subcommands.reload = {
  desc = "Rebuild the C++ library",
  usage = "reload",
  fn = function()
    if reload_in_progress then
      vim.notify("vimficiency reload already running", vim.log.levels.WARN)
      return
    end
    local build_dir = vim.fn.expand("~/Projects/vimficiency/build")
    if vim.fn.isdirectory(build_dir) == 0 then
      vim.notify("vimficiency reload: build dir missing: " .. build_dir,
        vim.log.levels.ERROR)
      return
    end

    reload_in_progress = true
    vim.notify("Rebuilding Vimficiency...", vim.log.levels.INFO)

    local tail_cap = 40
    local tail = {}
    local last_echo_ns = 0
    local echo_interval_ns = 150 * 1e6
    local pending_line = ""

    local function push_tail(line)
      tail[#tail + 1] = line
      if #tail > tail_cap then
        table.remove(tail, 1)
      end
    end

    local function echo_progress(msg)
      vim.schedule(function()
        vim.api.nvim_echo(
          { { "vimficiency rebuild: ", "MoreMsg" }, { msg, "Normal" } },
          false,
          {}
        )
      end)
    end

    local function handle_line(line)
      if line == "" then return end
      push_tail(line)
      local progress = parse_build_progress(line)
      if progress then
        local now = vim.uv.hrtime()
        if now - last_echo_ns >= echo_interval_ns then
          last_echo_ns = now
          echo_progress(line)
        end
      end
    end

    local function on_stream(_, data)
      if not data or data == "" then return end
      local chunk = pending_line .. data
      local start = 1
      while true do
        local nl = chunk:find("\n", start, true)
        if not nl then break end
        handle_line(chunk:sub(start, nl - 1))
        start = nl + 1
      end
      pending_line = chunk:sub(start)
    end

    local function on_exit(obj)
      reload_in_progress = false
      if pending_line ~= "" then
        handle_line(pending_line)
        pending_line = ""
      end
      vim.schedule(function()
        vim.api.nvim_echo({ { "", "Normal" } }, false, {})
        if obj.code == 0 then
          vim.notify(
            "Rebuild complete. Restart Neovim to load new library.",
            vim.log.levels.WARN
          )
        else
          local lines = { "vimficiency rebuild failed (exit " .. tostring(obj.code) .. "):" }
          for _, l in ipairs(tail) do
            lines[#lines + 1] = "  " .. l
          end
          vim.notify(table.concat(lines, "\n"), vim.log.levels.ERROR)
        end
      end)
    end

    local ok, err = pcall(vim.system, {
      "cmake", "--build", build_dir, "-j",
    }, {
      text = true,
      stdout = on_stream,
      stderr = on_stream,
    }, on_exit)

    if not ok then
      reload_in_progress = false
      vim.notify("vimficiency reload: failed to spawn: " .. tostring(err),
        vim.log.levels.ERROR)
    end
  end,
}

subcommands.config = {
  desc = "Show current configuration",
  usage = "config",
  fn = function()
    local debug_output = ffi_lib.debug_config()
    vim.cmd("botright new")
    local buf = vim.api.nvim_get_current_buf()
    vim.bo[buf].buftype = "nofile"
    vim.bo[buf].bufhidden = "wipe"
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, vim.split(debug_output, "\n"))
  end,
}

subcommands.help = {
  desc = "Show help",
  usage = "help",
  fn = function()
    vim.cmd("help vimficiency")
  end,
}

function M.run(subcmd, args, source)
  local cmd = subcommands[subcmd]
  if not cmd then
    local message = "Unknown subcommand: " .. tostring(subcmd)
    if source then
      message = "Vimficiency: unknown subcommand '" .. tostring(subcmd) .. "' from " .. source
    else
      message = message .. "\nRun :Vimfy help for usage"
    end
    vim.notify(message, vim.log.levels.ERROR)
    return false
  end
  cmd.fn(args or {})
  return true
end

function M.handle(arg_string)
  local args = vim.split(arg_string or "", "%s+")
  local subcmd = args[1] or ""

  if subcmd == "" then
    local cmd_lines = {}
    for _, cmd in pairs(subcommands) do
      table.insert(cmd_lines, string.format("  Vimfy %-20s %s", cmd.usage, cmd.desc))
    end
    table.sort(cmd_lines)
    util.show_output("Vimficiency Commands", table.concat(cmd_lines, "\n"))
    return
  end

  table.remove(args, 1)
  M.run(subcmd, args)
end

function M.complete(arg_lead, cmd_line, cursor_pos)
  local args = vim.split(cmd_line:sub(1, cursor_pos), "%s+")
  table.remove(args, 1)

  if #args <= 1 then
    local matches = {}
    for name, _ in pairs(subcommands) do
      if name:find("^" .. arg_lead) then
        table.insert(matches, name)
      end
    end
    table.sort(matches)
    return matches
  end

  local subcmd = args[1]

  if subcmd == "suggest" then
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, { "on", "off", "toggle" })
  end

  if subcmd == "view" or subcmd == "rm" then
    local saved = session.list_saved()
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, saved)
  end

  if subcmd == "start" or subcmd == "watch" then
    local aliases = vim.tbl_filter(alias_mod.is_valid_manual, session.list())
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, aliases)
  end

  if subcmd == "end" then
    local aliases = vim.tbl_filter(alias_mod.is_valid_manual, session.list())
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, aliases)
  end

  if subcmd == "recall" then
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, alias_mod.TIME_HINTS)
  end

  if subcmd == "close" or subcmd == "sim" then
    local aliases = session.list()
    for _, t in ipairs(alias_mod.TIME_HINTS) do
      table.insert(aliases, t)
    end
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, aliases)
  end

  if subcmd == "save" then
    if #args == 2 then
      local selectors = session.list()
      table.insert(selectors, "@")
      for _, t in ipairs(alias_mod.TIME_HINTS) do
        table.insert(selectors, t)
      end
      return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, selectors)
    end
    return {}
  end

  return {}
end

return M
