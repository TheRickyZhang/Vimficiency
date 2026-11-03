# vimficiency

[![CI](https://github.com/TheRickyZhang/Vimficiency/actions/workflows/bench.yml/badge.svg)](https://github.com/TheRickyZhang/Vimficiency/actions/workflows/bench.yml)
[![Release](https://img.shields.io/github/v/release/TheRickyZhang/Vimficiency)](https://github.com/TheRickyZhang/Vimficiency/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

https://github.com/user-attachments/assets/d32cb577-8b72-4301-85ea-dc2c3d706a04

One of the biggest challenges with Vim is knowing which of the many ways to perform an edit is the most efficient — or what all the applicable motions even are.

vimficiency watches how you edit and surfaces shorter keystroke sequences that would have produced the same result, with awareness of customizable per-key effort and the algorithmically-tractable subset of Vim's grammar. You keep editing the way you already do; the plugin runs in the background and lets you replay suggestions side-by-side to learn.

The benchmark dashboard with details on the search process is here:
https://therickyzhang.github.io/Vimficiency/

For the two algorithms underneath — how a buffer change is partitioned into
planned edits, and how each plan is turned into keystrokes by shortest-path
search — see [How it works](#how-it-works) below, or the interactive
walkthrough on the docs site:
https://therickyzhang.github.io/Vimficiency/docs/how-it-works/

- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Documentation](#documentation)
- [How it works](#how-it-works)
- [License](#license)

## Requirements

- Neovim 0.11+
- **Linux or macOS.** Windows is not yet supported.
- Prebuilt libraries are published for Linux x86_64, Linux aarch64, and
  macOS arm64 (built on Ubuntu 24.04; the macOS build runs on 14.0+). On other systems, or
  when installing from an untagged commit, the install step builds from
  source and needs a C++23 compiler (GCC 13+ or Clang 16+) and CMake 3.20+.

## Installation

vimficiency ships a native library (`libvimficiency.{so,dylib}`) that the
Lua side loads via LuaJIT FFI. `scripts/install.sh` installs it as the plugin
manager's build step: it downloads the prebuilt for your platform when the
checkout is on a release tag, and otherwise builds from source. Every snippet
below pins the latest release so you get the prebuilt; drop the pin to follow
`main`, which builds from source and needs the compiler from Requirements.

First install takes a few seconds for the download, or a few minutes when it
builds from source.

### lazy.nvim

```lua
{
  "TheRickyZhang/Vimficiency",
  version = "*",
  build = "./scripts/install.sh",
  config = function()
    require("vimficiency").setup()
  end,
}
```

If you update the plugin and start seeing an "ABI mismatch" error on load,
the shared library is stale against the new FFI bindings — run
`:Lazy build vimficiency` to reinstall it. For "could not load libvimficiency"
and other startup problems, see
[Troubleshooting](doc-src/14-troubleshooting.md).

### packer.nvim

```lua
use {
  "TheRickyZhang/Vimficiency",
  tag = "*",
  run = "./scripts/install.sh",
  config = function() require("vimficiency").setup() end,
}
```

### vim-plug

```vim
Plug 'TheRickyZhang/Vimficiency', { 'tag': '*', 'do': './scripts/install.sh' }
```

…then in your Lua config: `require("vimficiency").setup()`.

### Manual build

```bash
git clone --branch v0.1.1 https://github.com/TheRickyZhang/Vimficiency
cd Vimficiency
./scripts/install.sh
```

Or configure CMake yourself; this builds only the library, in Release:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF
cmake --build build -j --target vimficiency
```

Then either place the plugin on your `runtimepath` or point at the library
explicitly:

```bash
export VIMFICIENCY_LIB_PATH=/path/to/Vimficiency/build/libvimficiency.so   # .dylib on macOS
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
- **Limitations:** [`doc-src/13-limitations.md`](doc-src/13-limitations.md) — what the optimizer cannot model.
- **Internals:** `dev/` — implementation notes on the optimizer, FFI conventions, replay precompute, etc.

## How it works

A session gives the optimizer an initial buffer, a goal buffer, the cursor, and
what the user typed. Finding something shorter is two problems: a **partition**
of the change into planned edits, priced by a coarse keystroke heuristic, and a
**shortest-path search** over editor states that realizes each planned edit
with exact Vim semantics.

The planner lays the initial text against the goal on a grid, prices every
delete, move and type step with counted Vim commands, and seals long unchanged
runs so its work scales with the change rather than the buffer:

https://github.com/user-attachments/assets/d214130e-4026-4ea6-994d-fe85c3503277

The search then turns each planned edit into keystrokes with three A* searches
(navigation, transform, composition) over a Vim simulator verified against a
headless Neovim, priced by a per-key effort model:

https://github.com/user-attachments/assets/737cf2df-1d26-47b9-b947-d5159b5b586b

The full explanation, with interactive widgets that run the real planner on
text you type, is on the docs site:
**[How it works](https://therickyzhang.github.io/Vimficiency/docs/how-it-works/)**.

## License

MIT — see [LICENSE](LICENSE).
