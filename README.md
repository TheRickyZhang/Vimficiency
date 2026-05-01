# Vimfy

One of the biggest challenges with Vim is knowing which of the many ways to perform an edit is the most efficient — or what all the applicable motions even are.

Vimfy watches how you edit and surfaces shorter keystroke sequences that would have produced the same result, with awareness of customizable per-key effort and the algorithmically-tractable subset of Vim's grammar. You keep editing the way you already do; the plugin runs in the background and lets you replay suggestions side-by-side to learn.

The benchmark dashboard with details on the search process is here:
https://therickyzhang.github.io/Vimfy/

## Requirements
- Neovim 0.11+
- CMake 4.1+
- C++23

## Build
```bash
cmake -B build
cmake --build build
```

## Installation
```lua
require('vimficiency').setup()
```

For a full lazy.nvim-style setup with configuration and keymaps, see
[`examples/config.lua`](examples/config.lua).

Ensure `build/libvimficiency.so` is on your library path or set `VIMFICIENCY_LIB_PATH`.

## Usage

Vimfy organizes work around **sessions** — captures of (start state, keys typed, end state) that the optimizer scores. Sessions form a 2×2 over how they start and end:

|                   | **Manual end**                | **Auto end** (idle)         |
|-------------------|-------------------------------|-----------------------------|
| **Manual start**  | **Mark** — `:Vimfy start a` … `:Vimfy finish a` | **Watch** — `:Vimfy watch a`            |
| **Auto start**    | **Recall** — `:Vimfy recall 6` / `recall 3s` | **Suggest** — fires while you edit      |

Recall is always on, so the lowest-friction entry point is:

```vim
" ... edit something ...
:Vimfy recall 6     " analyze the last 6 keystrokes (or 'recall 3s' for time)
:Vimfy play 6       " animate the suggested sequence
```

For the full set of commands, save/store/fetch flow, configuration, and the effort model, see the user guide.

## Documentation

- **User guide:** [`doc-src/README.md`](doc-src/README.md) — installation, the four session types, keymap contract, configuration, effort model, troubleshooting.
- **In-editor:** `:help vimficiency` (generated from `doc-src/`).
- **Internals:** `dev/` — implementation notes on the optimizer, FFI conventions, replay precompute, etc.

## License

MIT
