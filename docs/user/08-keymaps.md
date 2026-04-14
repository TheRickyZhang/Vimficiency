**[← Results](07-results.md)** | **[Index](./README.md)** | **[Next: Configuration →](09-configuration.md)**

---

# 8. Binding keys to Vimfy actions

Vimficiency measures the keys you type. So a key that *invokes* Vimfy
should not itself be counted as a motion. Three ways to bind keys do
that correctly; one ergonomic way does not.

## 1. `vimfy.map()` (recommended)

The simplest path — one line per binding, handles all subcommands:

```lua
local vimfy = require('vimficiency')

vimfy.map('n', '<leader>vs', 'start a')
vimfy.map('n', '<leader>ve', 'end a')
vimfy.map('n', '<leader>vr', 'recall toggle')
vimfy.map('n', '<leader>vq', 'save @ quick')
```

The string argument is parsed like `:Vimfy <args>`: first word is the
subcommand, the rest are forwarded verbatim. Any `:Vimfy ...` command
line works as a `vimfy.map` spec.

The fourth argument is the `opts` table passed through to
`vim.keymap.set` (`buffer`, `desc`, `silent`, ...). `silent` defaults
to `true`; `desc` defaults to `"Vimficiency <spec>"`.

### Binding arbitrary Lua

Pass a function instead of a string when you need to branch or compose:

```lua
vimfy.map('n', 'Z', function()
  if vim.bo.buftype == "" then
    vim.cmd('Vimfy start a')
  end
end)
```

The callback is wrapped with `M.wrap` internally, so any `:Vimfy` or
plugin-level calls you make from it are announced as admin activity.

## 2. `<Plug>` maps

For users who prefer the Vim convention, each common action has a
`<Plug>` name. Bind with `remap = true` (or `nmap` in vimscript):

```vim
nmap <leader>vs <Plug>VimfyStartA
nmap <leader>ve <Plug>VimfyEndA
```

```lua
vim.keymap.set('n', '<leader>vs', '<Plug>VimfyStartA', { remap = true })
```

**Available `<Plug>` names:**

| Name                                 | Action                               |
|--------------------------------------|--------------------------------------|
| `<Plug>VimfyStart{A,B,C,D,E}`        | `:Vimfy start <alias>`               |
| `<Plug>VimfyWatch{A,B,C,D,E}`        | `:Vimfy watch <alias>`               |
| `<Plug>VimfyEnd{A,B,C,D,E}`          | `:Vimfy end <alias>`                 |
| `<Plug>VimfyClose{A,B,C,D,E}`        | `:Vimfy close <alias>`               |
| `<Plug>VimfySim{A,B,C,D,E}`          | `:Vimfy sim <alias>`                 |
| `<Plug>VimfyRecall{On,Off,Toggle}`   | `:Vimfy recall <on\|off\|toggle>`    |
| `<Plug>VimfySuggest{On,Off,Toggle}`  | `:Vimfy suggest <on\|off\|toggle>`   |
| `<Plug>VimfyList`                    | `:Vimfy list`                        |
| `<Plug>VimfyConfig`                  | `:Vimfy config`                      |
| `<Plug>VimfyHelp`                    | `:Vimfy help`                        |

`<Plug>` covers the common cases; `vimfy.map()` covers everything
(including arbitrary args, e.g. `save @ quick`, which no `<Plug>`
name exposes directly).

## 3. `require('vimficiency').wrap(fn)` (low-level)

`vimfy.map(mode, lhs, fn, opts)` is shorthand for
`vim.keymap.set(mode, lhs, wrap(fn), opts)`. If you need to attach
your Lua callback somewhere other than a keymap — an autocommand, a
command, a UI callback — call `wrap` directly:

```lua
local vimfy = require('vimficiency')
vim.api.nvim_create_autocmd('User', {
  pattern = 'MyEvent',
  callback = vimfy.wrap(function() vim.cmd('Vimfy end a') end),
})
```

## Direct `:Vimfy ...` typing

Works as-is. Cmdline input is never counted as motion, so nothing to wrap.

## What doesn't work

A `nnoremap`/`nmap` whose RHS is a literal Ex command:

```vim
nnoremap X :Vimfy start a<CR>
nmap     Y <Cmd>Vimfy start a<CR>
```

The `X` or `Y` keystroke is delivered to Vimfy's tracker *before* the
mapping resolves — we can't retroactively uncount it. To fix: move the
binding into `vimfy.map()` or a `<Plug>` map.

At setup, Vimficiency scans pre-existing mappings for this pattern and
emits a one-shot warning listing each offender. The scan can't see
mappings defined *after* setup (or Lua-callback RHS, or buffer-local
mappings added later) — if you keep seeing the LHS counted as motion,
check that the binding is routed through `vimfy.map()` / `<Plug>` /
`wrap()`.

## Why the contract

`vim.on_key` fires for the LHS keystroke *before* Neovim resolves the
mapping. By the time the RHS runs (whether it's `:Vimfy ...`, a
`<Plug>` map, or a Lua callback), the LHS has already been delivered.
We can't auto-suppress; the caller has to announce. `vimfy.map()`,
`<Plug>Vimfy*`, and `wrap()` are three ways to spell that announcement.

---

**[← Results](07-results.md)** | **[Index](./README.md)** | **[Next: Configuration →](09-configuration.md)**
