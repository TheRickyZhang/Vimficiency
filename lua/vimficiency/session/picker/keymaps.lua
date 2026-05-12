local M = {}

function M.install(buf, actions)
  local function map(lhs, fn)
    vim.keymap.set("n", lhs, fn, { buffer = buf, nowait = true, silent = true })
  end
  map("q", actions.close)
  map("<Esc>", actions.close)
  map("<Tab>", actions.toggle_pane)
  map("sn", function() actions.sort_apply("alpha", false) end)
  map("sN", function() actions.sort_apply("alpha", true) end)
  map("sc", function() actions.sort_apply("category", false) end)
  map("sC", function() actions.sort_apply("category", true) end)
  map("st", function() actions.sort_apply("created", false) end)
  map("sT", function() actions.sort_apply("created", true) end)
  map("s?", actions.sort_popup)
  map("s", actions.sort_popup)
  map("/", actions.search)
  map("<CR>", actions.open)
  map("d", actions.delete)
  map("D", actions.delete_marked)
  map("m", actions.mark)
  map("r", actions.rename)
  map("y", actions.duplicate)
  map("?", actions.help)
end

function M.install_prompt(prompt_buf, actions)
  local function pmap(mode, lhs, fn)
    vim.keymap.set(mode, lhs, fn,
      { buffer = prompt_buf, nowait = true, silent = true })
  end
  pmap("i", "<CR>", function() actions.exit_prompt_to_list(); actions.open() end)
  pmap("i", "<Esc>", actions.exit_prompt_to_list)
  pmap("i", "<C-n>", function() actions.move_list(1) end)
  pmap("i", "<C-p>", function() actions.move_list(-1) end)
  pmap("i", "<Down>", function() actions.move_list(1) end)
  pmap("i", "<Up>", function() actions.move_list(-1) end)
  pmap("n", "<CR>", function() actions.exit_prompt_to_list(); actions.open() end)
  pmap("n", "<Esc>", actions.exit_prompt_to_list)
  pmap("n", "q", actions.exit_prompt_to_list)
end

return M
