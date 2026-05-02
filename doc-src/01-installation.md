---
title: "Installation"
---

# Installation

For requirements, plugin-manager snippets (lazy.nvim, packer, vim-plug), and
manual-build instructions, see the top-level [`README.md`](../README.md).
That is the canonical install reference; this page only covers what comes
*after* the library is built.

## Setup

```lua
require('vimficiency').setup()
```

For a complete setup with configuration and keymaps, use
[`examples/config.lua`](../examples/config.lua) as the canonical sample.

You can pass config overrides to `setup{}` — see
[9. Configuration](09-configuration.md).

Bindings must route through `vimfy.map()`, `<Plug>Vimfy*`, or `wrap()`;
see the keymap contract in the [Index](./README.md) and the full writeup
in [8. Keymaps](08-keymaps.md).

## Verifying installation

```vim
:Vimfy help
```

If this prints the command list, you're ready.

If instead you see "command not defined", `setup()` didn't run — check your
config for syntax errors or a load-order issue, or see
[14. Troubleshooting](14-troubleshooting.md).

If you see an "ABI mismatch" error on load, the shared library is stale
against the Lua FFI bindings — rebuild the plugin (`:Lazy build vimficiency`
or `cmake --build <plugin-dir>/build`).
