local M = {}

function M.check()
  vim.health.start("vimficiency")

  if vim.fn.has("nvim-0.11") == 1 then
    vim.health.ok("Neovim " .. tostring(vim.version()))
  else
    vim.health.error("Neovim 0.11 or newer is required")
  end

  local ok, core = pcall(require, "vimficiency.ffi.core")
  if not ok then
    vim.health.error("native library failed to load:\n" .. tostring(core), {
      "Run `:Lazy build vimficiency`, or `./scripts/install.sh` in the plugin directory",
      "See :help vimficiency-troubleshooting",
    })
    return
  end
  vim.health.ok(string.format("native library %s loaded from %s",
    core.version_string(), core.lib_path))
  vim.health.ok("ABI hash matches src/LuaExports/Api.def")

  if vim.fn.exists(":Vimfy") == 2 then
    vim.health.ok("setup() has run; `:Vimfy` is defined")
  else
    vim.health.warn("setup() has not run", {
      "Add `require('vimficiency').setup()` to your config",
    })
  end
end

return M
