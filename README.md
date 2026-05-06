# vimficiency

https://github.com/user-attachments/assets/d32cb577-8b72-4301-85ea-dc2c3d706a04

One of the biggest challenges with Vim is knowing which of the many ways to perform an edit is the most efficient — or what all the applicable motions even are.

vimficiency watches how you edit and surfaces shorter keystroke sequences that would have produced the same result, with awareness of customizable per-key effort and the algorithmically-tractable subset of Vim's grammar. You keep editing the way you already do; the plugin runs in the background and lets you replay suggestions side-by-side to learn.

The benchmark dashboard with details on the search process is here:
https://therickyzhang.github.io/Vimfy/

## Requirements

- Neovim 0.11+
- A C++23 compiler (GCC 13+ or Clang 16+) and CMake 4.1+ to build the native
  library. **Linux and macOS only** at the moment — Windows is not yet
  supported. Prebuilt binaries are not yet shipped; the plugin is currently
  built from source on install.

## Installation

vimficiency ships a native library (`libvimficiency.{so,dylib}`) that the
Lua side loads via LuaJIT FFI. Plugin managers can compile it as part of the
install step.

### lazy.nvim

```lua
{
  "therickyzhang/vimficiency",
  build = "cmake -B build && cmake --build build",
  config = function()
    require("vimficiency").setup()
  end,
}
```

If you update the plugin and start seeing an "ABI mismatch" error on load,
the prebuilt artifact is stale against the new FFI bindings — run
`:Lazy build vimficiency` to recompile.

### packer.nvim

```lua
use {
  "therickyzhang/vimficiency",
  run = "cmake -B build && cmake --build build",
  config = function() require("vimficiency").setup() end,
}
```

### vim-plug

```vim
Plug 'therickyzhang/vimficiency', { 'do': 'cmake -B build && cmake --build build' }
```

…then in your Lua config: `require("vimficiency").setup()`.

### Manual build

```bash
git clone https://github.com/therickyzhang/vimficiency
cd vimficiency
cmake -B build
cmake --build build
```

Then either place the plugin on your `runtimepath` or point at the library
explicitly:

```bash
export VIMFICIENCY_LIB_PATH=/path/to/vimficiency/build/libvimficiency.so
```

For a full setup with configuration and keymaps, see
[`examples/config.lua`](examples/config.lua).

## Usage

vimficiency organizes work around **sessions** — captures of (start state, keys typed, end state) that the optimizer scores. Sessions form a 2×2 over how they start and end:

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
