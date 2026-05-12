local ffi_core = require("vimficiency.ffi.core")
local session = require("vimficiency.session")
local auto_suggest = require("vimficiency.capture.auto_suggest")
local alias_mod = require("vimficiency.session.alias")
local util = require("vimficiency.util")
local explore = require("vimficiency.explore")
local explore_result = require("vimficiency.explore.result")

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

subcommands.finish = {
  desc = "Finish a manual session and show results",
  usage = "finish <alias>",
  fn = function(args)
    local alias = args[1]
    if not alias or alias == "" then
      vim.notify("Usage: Vimfy finish <alias>", vim.log.levels.ERROR)
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

subcommands.play = {
  desc = "Play your sequence side-by-side with the optimizer's suggestions",
  usage = "play <alias> [windows]",
  fn = function(args)
    local alias = args[1]
    local window_count = args[2] and tonumber(args[2]) or nil
    if not alias or alias == "" then
      vim.notify("Usage: Vimfy play <alias> [windows]  (1..4)", vim.log.levels.ERROR)
      return
    end
    session.simulate(alias, window_count)
  end,
}

subcommands.focus = {
  desc = "Focus replay on the Nth buffer",
  usage = "focus <N>",
  fn = function(args)
    local n = args[1] and tonumber(args[1])
    if not n or n < 1 or n ~= math.floor(n) then
      vim.notify("Usage: Vimfy focus <N>  (1-indexed integer)", vim.log.levels.ERROR)
      return
    end
    require("vimficiency.simulate").focus(n)
  end,
}

subcommands.escape = {
  desc = "Restore the side-by-side replay layout",
  usage = "escape",
  fn = function()
    require("vimficiency.simulate").escape()
  end,
}

--- Resolve the optional `[<name>]` arg for save/store.
--- Explicit arg wins; otherwise defer to `session.default_save_name()`.
---@param selector string
---@param explicit string|nil
---@return string|nil
local function resolve_save_name(selector, explicit)
  if explicit and explicit ~= "" then return explicit end
  return session.default_save_name(selector)
end

subcommands.save = {
  desc = "Copy a finished session result to disk (keeps the session copy)",
  usage = "save <selector>|@ [<name>]",
  fn = function(args)
    local selector = args[1]
    if not selector or selector == "" then
      vim.notify("Usage: Vimfy save <selector>|@ [<name>]", vim.log.levels.ERROR)
      return
    end
    local name = resolve_save_name(selector, args[2])
    if not name then
      vim.notify("save @: no recently finished session. Run ':Vimfy finish <alias>' first.",
        vim.log.levels.ERROR)
      return
    end
    session.save(selector, name)
  end,
}

subcommands.store = {
  desc = "Move a finished session from memory to disk (removes the session copy)",
  usage = "store <selector>|@ [<name>]",
  fn = function(args)
    local selector = args[1]
    if not selector or selector == "" then
      vim.notify("Usage: Vimfy store <selector>|@ [<name>]", vim.log.levels.ERROR)
      return
    end
    local name = resolve_save_name(selector, args[2])
    if not name then
      vim.notify("store @: no recently finished session. Run ':Vimfy finish <alias>' first.",
        vim.log.levels.ERROR)
      return
    end
    session.store(selector, name)
  end,
}

subcommands.fetch = {
  desc = "Copy a saved result from disk into the current session",
  usage = "fetch <name> [<alias>]",
  fn = function(args)
    local name = args[1]
    if not name or name == "" then
      vim.notify("Usage: Vimfy fetch <name> [<alias>]", vim.log.levels.ERROR)
      return
    end
    local alias = (args[2] and args[2] ~= "") and args[2] or name
    session.fetch(name, alias)
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
  desc = "Open the interactive session picker",
  usage = "list",
  fn = function()
    require("vimficiency.session.picker").open()
  end,
}

subcommands.stats = {
  desc = "Show lifetime session stats (efficiency, motions, beats)",
  usage = "stats",
  fn = function()
    require("vimficiency.stats").open()
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
        vim.notify("vimfy auto-suggest enabled", vim.log.levels.INFO)
      else
        vim.notify("vimfy auto-suggest already enabled", vim.log.levels.WARN)
      end
    end

    if action == "on" then
      enable_or_warn()
    elseif action == "off" then
      auto_suggest.disable()
      vim.notify("vimfy auto-suggest disabled", vim.log.levels.INFO)
    elseif action == "toggle" then
      if auto_suggest.is_enabled() then
        auto_suggest.disable()
        vim.notify("vimfy auto-suggest disabled", vim.log.levels.INFO)
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
      vim.notify("vimfy reload already running", vim.log.levels.WARN)
      return
    end
    -- Resolve relative to the plugin root. A sibling `build/` is only expected
    -- in dev installs; packaged installs ship the pre-built `.so`.
    local build_dir = util.find_plugin_root() .. "/build"
    if vim.fn.isdirectory(build_dir) == 0 then
      vim.notify("vimfy reload: build dir missing: " .. build_dir ..
        "\n(this command is dev-only; install via a plugin manager bundles a pre-built .so)",
        vim.log.levels.ERROR)
      return
    end

    reload_in_progress = true
    vim.notify("Rebuilding Vimfy...", vim.log.levels.INFO,
      { title = "Vimfy" })

    local output = {}
    local pending_line = ""

    local function on_stream(_, data)
      if not data or data == "" then return end
      local chunk = pending_line .. data
      local start = 1
      while true do
        local nl = chunk:find("\n", start, true)
        if not nl then break end
        local line = chunk:sub(start, nl - 1)
        if line ~= "" then output[#output + 1] = line end
        start = nl + 1
      end
      pending_line = chunk:sub(start)
    end

    local function on_exit(obj)
      reload_in_progress = false
      if pending_line ~= "" then
        output[#output + 1] = pending_line
        pending_line = ""
      end
      vim.schedule(function()
        if obj.code == 0 then
          local progress_lines = {}
          for _, l in ipairs(output) do
            if parse_build_progress(l) then
              progress_lines[#progress_lines + 1] = l
            end
          end

          -- To pick up the freshly built C++, we need dlopen to return a
          -- new handle. Glibc's dlopen dedups by (dev, inode) — so
          -- overwriting libvimficiency.so in place, or hardlinking it,
          -- both yield cache hits. The only reliable way to defeat
          -- dedup is to present a file with a distinct inode, which
          -- means copying.
          --
          -- But the file only has to exist at the moment `ffi.load`
          -- runs: once dlopen has mmap'd the image, the code lives in
          -- memory independently of the directory entry. Unlinking
          -- immediately after a successful load keeps build/ clean and
          -- avoids accumulating `libvimficiency-reload-*.so` artifacts
          -- across sessions.
          local src = build_dir .. "/libvimficiency.so"
          -- `%.0f` avoids scientific-notation filenames: `tostring` on a
          -- Lua number >= ~2^53 (hrtime ns reaches that after ~104 days
          -- of uptime) formats as `1.0742e+14`, an ugly filename. `%.0f`
          -- always emits decimal form.
          local unique = build_dir .. "/libvimficiency-reload-"
            .. string.format("%.0f", vim.uv.hrtime()) .. ".so"
          local copied = false
          local copy_ok, copy_err = pcall(function()
            copied = vim.uv.fs_copyfile(src, unique)
          end)
          if not copy_ok or not copied then
            vim.notify(
              "vimfy reload: rebuild succeeded but copying " ..
              "libvimficiency.so for reload failed: " .. tostring(copy_err or "?")
              .. " — restart Neovim to load new library.",
              vim.log.levels.WARN, { title = "Vimfy" })
            return
          end

          local reload_ok, reload_err = pcall(function()
            return require("vimficiency").reload_lua(unique)
          end)

          -- Drop the on-disk copy regardless of outcome. On success the
          -- mmap'd image survives via its inode; on failure the file is
          -- useless anyway. Done here (not inside reload_lua) so the
          -- reload API stays agnostic about how its path argument was
          -- produced.
          pcall(vim.uv.fs_unlink, unique)

          if not reload_ok then
            vim.notify(
              "vimfy reload: Lua reload crashed: " ..
              tostring(reload_err) .. " — restart Neovim.",
              vim.log.levels.ERROR, { title = "Vimfy" })
            return
          end

          local r = reload_err  -- pcall's second return on success
          local summary = string.format(
            "Rebuild + Lua reload complete.\n" ..
            "  preserved finished records: %d\n" ..
            "  dropped active captures: %d",
            (r and r.preserved_records) or 0,
            (r and r.dropped_active) or 0)
          local msg = summary
          if #progress_lines > 0 then
            msg = table.concat(progress_lines, "\n") .. "\n\n" .. summary
          end
          vim.notify(msg, vim.log.levels.INFO, { title = "Vimfy" })
        else
          local lines = { "vimfy rebuild failed (exit " .. tostring(obj.code) .. "):" }
          local start_idx = math.max(1, #output - 39)  -- last 40 lines
          for i = start_idx, #output do
            lines[#lines + 1] = "  " .. output[i]
          end
          vim.notify(table.concat(lines, "\n"), vim.log.levels.ERROR,
            { title = "Vimfy" })
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
      vim.notify("vimfy reload: failed to spawn: " .. tostring(err),
        vim.log.levels.ERROR)
    end
  end,
}

subcommands.config = {
  desc = "Show current configuration",
  usage = "config",
  fn = function()
    local debug_output = ffi_core.debug_config()
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

subcommands.explore = {
  desc = "Open the interactive explore scratch view for a finished result",
  usage = "explore <selector>|@",
  fn = function(args)
    local selector = args[1]
    if not selector or selector == "" then
      vim.notify("Usage: Vimfy explore <selector>|@", vim.log.levels.ERROR)
      return
    end
    local result, err = session.resolve_result(selector)
    if not result then
      vim.notify("vimfy explore failed: " .. (err or "unknown error"), vim.log.levels.ERROR)
      return
    end
    local valid, validation_err = explore_result.validate(result)
    if not valid then
      vim.notify("vimfy explore failed: " .. validation_err, vim.log.levels.ERROR)
      return
    end
    ---@cast result VF.Explore.OpenResult
    explore.open(selector, result)
  end,
}

function M.run(subcmd, args, source)
  local cmd = subcommands[subcmd]
  if not cmd then
    local message = "Unknown subcommand: " .. tostring(subcmd)
    if source then
      message = "Vimfy: unknown subcommand '" .. tostring(subcmd) .. "' from " .. source
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
    util.show_output("Vimfy Commands", table.concat(cmd_lines, "\n"), {
      ui_keys = {
        title = "Vimfy Scratch Output Keys",
        docs = true,
      },
    })
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

  if subcmd == "finish" then
    local aliases = vim.tbl_filter(alias_mod.is_valid_manual, session.list())
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, aliases)
  end

  if subcmd == "recall" then
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, alias_mod.TIME_HINTS)
  end

  if subcmd == "close" then
    local aliases = session.list()
    for _, t in ipairs(alias_mod.TIME_HINTS) do
      table.insert(aliases, t)
    end
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, aliases)
  end

  if subcmd == "play" then
    local candidates = session.list()
    for _, t in ipairs(alias_mod.TIME_HINTS) do
      table.insert(candidates, t)
    end
    for _, name in ipairs(session.list_saved()) do
      table.insert(candidates, name)
    end
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, candidates)
  end

  if subcmd == "explore" then
    local candidates = session.list()
    table.insert(candidates, "@")
    for _, t in ipairs(alias_mod.TIME_HINTS) do
      table.insert(candidates, t)
    end
    for _, name in ipairs(session.list_saved()) do
      table.insert(candidates, name)
    end
    return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, candidates)
  end

  if subcmd == "save" or subcmd == "store" then
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

  if subcmd == "fetch" then
    if #args == 2 then
      local saved = session.list_saved()
      return vim.tbl_filter(function(v) return v:find("^" .. arg_lead) end, saved)
    end
    return {}
  end

  return {}
end

return M
