**[← Workflows](12-workflows.md)** | **[Index](./README.md)** | **[Next: Troubleshooting →](14-troubleshooting.md)**

---

# 13. Known limitations

- **Text-object final character** is sometimes missing from the recorded
  sequence (e.g., `ciw` may record as `c`+`i`). The optimizer still analyzes
  the resulting state correctly, but your `user:` sequence in the output may
  look truncated.

- **Screen-line motions** `gj`/`gk` are approximated as `j`/`k`. Exact on
  unwrapped lines; off by a little when wrapping is in play.

- **Multi-buffer edits** aren't supported within a single session. If you
  finish a session while a different buffer is focused, it is rejected.
  Transient window changes (floating pickers, temporary splits) are fine —
  the session stays alive and keys typed outside the session window are
  silently ignored.

- **Large regions** (more than `MAX_SEARCH_LINES`) are refused with an error;
  split the edit up or increase the limit if you really want this.

- **Long sessions** may hit the optimizer's search budget; expect diminishing
  suggestion quality beyond roughly a dozen lines of edits.

- **Unwrapped mappings that invoke Vimfy** have their trigger keypress
  counted as motion. See [8. Keymaps](08-keymaps.md) for the fix.

- **Recall may include the final char of a `<Plug>`-mapped LHS** on
  occasion, because `vim.on_key` fires for that char before Neovim
  resolves the mapping. If you target `:Vimfy recall N` precisely,
  count `N` accordingly — or use time recall (`Ns`), which rolls past
  the extra key without noticing.

- **Recall only records keystrokes typed in normal file buffers**
  (`&buftype == ""`). Keys typed in help, quickfix, terminal, prompt,
  or scratch buffers are skipped — they aren't meaningful for motion
  optimization, and recording them would cause recall queries to fail
  with buffer-mismatch when you return to your file. If you bind a
  Vimfy action that opens an async floating-input prompt (e.g.
  `vim.ui.input` overridden by dressing.nvim), the `2` / `<CR>` you
  type into that prompt are filtered automatically.

---

**[← Workflows](12-workflows.md)** | **[Index](./README.md)** | **[Next: Troubleshooting →](14-troubleshooting.md)**
