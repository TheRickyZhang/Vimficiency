local v = vim.api

local M = {}

local SORT_HINT_LINES = {
  "  Sort by…",
  "",
  "  n / N   name       (A→Z / Z→A)",
  "  c / C   category   (A→Z / Z→A)",
  "  t / T   time       (newest / oldest)",
  "",
  "  <Esc>   cancel",
}

function M.sort(apply_sort)
  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  v.nvim_buf_set_lines(buf, 0, -1, false, SORT_HINT_LINES)
  vim.bo[buf].modifiable = false
  local width = 44
  local height = #SORT_HINT_LINES
  local row = math.floor((vim.o.lines - height) / 2)
  local col = math.floor((vim.o.columns - width) / 2)
  local win = v.nvim_open_win(buf, false, {
    relative = "editor", row = row, col = col,
    width = width, height = height, border = "rounded", style = "minimal",
    title = " Sort ", title_pos = "center", focusable = false,
  })
  vim.cmd("redraw")
  local ok, ch = pcall(vim.fn.getcharstr)
  pcall(v.nvim_win_close, win, true)
  if not ok or ch == "" or ch == "\27" then return end
  local mode_of = { n = "alpha", c = "category", t = "created" }
  local m = mode_of[ch:lower()]
  if not m then return end
  apply_sort(m, ch:match("%u") ~= nil)
end

local HELP_LINES = {
  "  Vimfy session picker",
  "",
  "  /          fuzzy search",
  "  <Tab>      switch Active ↔ Saved pane",
  "  sn / sN    sort by name      (A→Z / Z→A)",
  "  sc / sC    sort by category  (A→Z / Z→A)",
  "  st / sT    sort by time      (newest / oldest)",
  "  s          sort menu (s? shows the hint popup)",
  "  <CR>       open",
  "  d          delete",
  "  m          toggle mark",
  "  D          delete marked",
  "  r          rename",
  "  y          duplicate",
  "  ?          this help",
  "  q / <Esc>  close",
}

function M.help()
  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  v.nvim_buf_set_lines(buf, 0, -1, false, HELP_LINES)
  vim.bo[buf].modifiable = false
  local width = 56
  local height = #HELP_LINES + 2
  local row = math.floor((vim.o.lines - height) / 2)
  local col = math.floor((vim.o.columns - width) / 2)
  local win = v.nvim_open_win(buf, true, {
    relative = "editor", row = row, col = col,
    width = width, height = height, border = "rounded", style = "minimal",
  })
  vim.keymap.set("n", "q", function() pcall(v.nvim_win_close, win, true) end,
    { buffer = buf, nowait = true })
  vim.keymap.set("n", "<Esc>", function() pcall(v.nvim_win_close, win, true) end,
    { buffer = buf, nowait = true })
  vim.keymap.set("n", "?", function() pcall(v.nvim_win_close, win, true) end,
    { buffer = buf, nowait = true })
end

return M
