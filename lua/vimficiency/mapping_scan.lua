local M = {}

local MODES_TO_SCAN = { "n", "v", "x", "s", "o", "i", "t" }

local VIMFY_RHS_PATTERNS = {
  "^%s*:vimfy%f[%W]",
  "^%s*:vimficiency%f[%W]",
  "<cmd>%s*:?vimfy%f[%W]",
  "<cmd>%s*:?vimficiency%f[%W]",
}

function M.scan_rhs_for_vimfy(rhs)
  if type(rhs) ~= "string" or rhs == "" then return false end
  local lower = rhs:lower()
  for _, pattern in ipairs(VIMFY_RHS_PATTERNS) do
    if lower:match(pattern) then return true end
  end
  return false
end

function M.warn_about_bad_mappings()
  local bad = {}
  for _, mode in ipairs(MODES_TO_SCAN) do
    local ok, maps = pcall(vim.api.nvim_get_keymap, mode)
    if ok and type(maps) == "table" then
      for _, map in ipairs(maps) do
        if M.scan_rhs_for_vimfy(map.rhs) then
          table.insert(bad, string.format("  %s %-20s %s", mode, map.lhs or "?", map.rhs))
        end
      end
    end
  end
  if #bad == 0 then return end

  table.sort(bad)
  vim.notify(
    "vimficiency: detected " .. #bad ..
    " mapping(s) whose RHS invokes :Vimfy as an Ex command.\n" ..
    "These will count the LHS keystroke as motion. Migrate to\n" ..
    "`require('vimficiency').map()` or a `<Plug>Vimfy*` map:\n" ..
    table.concat(bad, "\n"),
    vim.log.levels.WARN
  )
end

return M
