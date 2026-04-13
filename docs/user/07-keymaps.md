**[← Results](06-results.md)** | **[Index](./README.md)** | **[Next: Configuration →](08-configuration.md)**

---

# 7. Binding keys to Vimfy actions

If you bind a key to a Vimfy command, you don't want the key itself counted
as a motion. Vimficiency provides two ways to declare that a keypress is
admin activity rather than editing activity.

## 1. `<Plug>` maps

The plugin exports one `<Plug>` per common action. Bind your preferred key
with `remap = true` (or `nmap`):

```vim
nmap <leader>vs <Plug>VimfyStartA
nmap <leader>ve <Plug>VimfyEndA
nmap <leader>vc <Plug>VimfyCloseA
nmap <leader>vv <Plug>VimfySimA
nmap <leader>vs <Plug>VimfySuggestToggle
nmap <leader>vr <Plug>VimfyRecallToggle
nmap <leader>vl <Plug>VimfyList
```

Or in Lua:
```lua
vim.keymap.set('n', '<leader>vs', '<Plug>VimfyStartA', { remap = true })
```

**Available `<Plug>` names:**

| Name                                            | Action                               |
|-------------------------------------------------|--------------------------------------|
| `<Plug>VimfyStart{A,B,C,D,E}`                   | `:Vimfy start <alias>`               |
| `<Plug>VimfyEnd{A,B,C,D,E}`                     | `:Vimfy end <alias>`                 |
| `<Plug>VimfyClose{A,B,C,D,E}`                   | `:Vimfy close <alias>`               |
| `<Plug>VimfySim{A,B,C,D,E}`                     | `:Vimfy sim <alias>`                 |
| `<Plug>VimfyRecall{On,Off,Toggle}`              | `:Vimfy recall <on\|off\|toggle>`    |
| `<Plug>VimfySuggest{On,Off,Toggle}`             | `:Vimfy suggest <on\|off\|toggle>`   |
| `<Plug>VimfyList`                               | `:Vimfy list`                        |
| `<Plug>VimfyConfig`                             | `:Vimfy config`                      |
| `<Plug>VimfyHelp`                               | `:Vimfy help`                        |

## 2. `require('vimficiency').wrap(fn)` for Lua callbacks

If your binding needs to do more than fire a single subcommand (e.g., prompt
for input, branch on state), wrap the callback:

```lua
vim.keymap.set('n', '<leader>vs', require('vimficiency').wrap(function()
  vim.cmd('Vimfy start a')
  -- ...or any other Vimfy-related Lua
end))
```

`wrap` declares to Vimfy's key tracker that everything inside this function
is admin activity and should not be recorded as motion.

## Direct `:Vimfy ...` typing

Works as-is. Cmdline input is never counted as motion, so no wrapping needed.

## What doesn't work

An unwrapped mapping whose RHS calls Vimfy, for example:

```vim
nnoremap X :Vimfy start a<CR>
```

Here the `X` keystroke itself will be recorded as a motion — Vimfy can't
tell it triggered the command. To fix: route the mapping through `<Plug>`
or `wrap()` instead:

```vim
nmap X <Plug>VimfyStartA
```

## Why the contract

Neovim's key event API (`vim.on_key`) cannot introspect what a mapping or
Lua callback will do — by the time Vimfy learns a mapping fired, its
keystrokes have already been delivered as if they were edits. The
`<Plug>` + `wrap()` contract lets the caller *announce* intent, which is
robust. See the codebase `docs/` for the design rationale.

---

**[← Results](06-results.md)** | **[Index](./README.md)** | **[Next: Configuration →](08-configuration.md)**
