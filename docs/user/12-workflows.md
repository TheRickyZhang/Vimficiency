**[← Commands](11-commands.md)** | **[Index](./README.md)** | **[Next: Limitations →](13-limitations.md)**

---

# 12. Recommended workflows

## "I'm actively trying to learn better motions"

Enable Suggest and the recall queue in your config:

```lua
require('vimficiency').setup({
    auto_suggest = { idle = { ms = 3000, window = "3s" } },
})
```

Edit naturally. Every pause surfaces suggestions. When something catches
your eye, `:Vimfy sim 3s` (or whatever window the suggestion targeted)
to see it animated.

Pair with [8. Keymaps](08-keymaps.md):
```vim
nmap <leader>vv <Plug>VimfySimA  " or ...bind to SimA / SimB / etc.
```
...so replaying a suggestion is a single keystroke.

## "I want to audit a specific edit I'm about to do"

Mark sessions give the cleanest boundaries:

```vim
:Vimfy start a
" ... the edit ...
:Vimfy end a
:Vimfy sim a
```

## "I know the start but not when I'll stop"

Watch it — same precise start, but let Vimfy auto-finish when you pause:

```lua
require('vimficiency').setup({ watch = { idle = { ms = 3000 }, cooldown_ms = 5000 } })
```

```vim
:Vimfy watch a
" ... edit normally, and stop when done ...
" ... after idle.ms of idleness, the optimizer runs and the result notifies ...
```

## "I just did something clumsy — was there a better way?"

Recall is built for this:

```vim
:Vimfy recall on    " do this once, leave on
" ... clumsy edit, say 5 keys ...
:Vimfy end 5        " or `end 3s` if time is the easier estimate
:Vimfy sim 5
```

## "I want to revisit this later / share it"

Save to disk after ending — `@` resolves to the session you just finished:

```vim
:Vimfy end a
:Vimfy save @ nested-dict-refactor
" ... days later ...
:Vimfy view nested-dict-refactor
```

Saved results are durable across Neovim restarts and survive in
`stdpath('data')/vimficiency/saved/` — you can share the file directly if
you want to show a colleague.

---

**[← Commands](11-commands.md)** | **[Index](./README.md)** | **[Next: Limitations →](13-limitations.md)**
