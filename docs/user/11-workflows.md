**[← Commands](10-commands.md)** | **[Index](./README.md)** | **[Next: Limitations →](12-limitations.md)**

---

# 11. Recommended workflows

## "I'm actively trying to learn better motions"

Enable auto-suggest and the recall ring in your config:

```lua
require('vimficiency').setup({
    auto_suggest = { idle = { ms = 3000, window = "3s" } },
})
```

Edit naturally. Every pause surfaces suggestions. When something catches
your eye, `:Vimfy sim 3s` (or whatever window the suggestion targeted)
to see it animated.

Pair with [7. Keymaps](07-keymaps.md):
```vim
nmap <leader>vv <Plug>VimfySimA  " or ...bind to SimA / SimB / etc.
```
...so replaying a suggestion is a single keystroke.

## "I want to audit a specific edit I'm about to do"

Manual sessions give the cleanest boundaries:

```vim
:Vimfy start a
" ... the edit ...
:Vimfy end a
:Vimfy sim a
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
:Vimfy save @ as nested-dict-refactor
" ... days later ...
:Vimfy view nested-dict-refactor
```

Saved results are durable across Neovim restarts and survive in
`stdpath('data')/vimficiency/saved/` — you can share the file directly if
you want to show a colleague.

---

**[← Commands](10-commands.md)** | **[Index](./README.md)** | **[Next: Limitations →](12-limitations.md)**
