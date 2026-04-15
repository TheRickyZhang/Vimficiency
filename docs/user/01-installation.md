**[Index](./README.md)** | **[Next: Sessions →](02-sessions.md)**

---

# 1. Installation

## Requirements

- Neovim 0.11+
- A built `libvimficiency.so` on disk (see the top-level `README.md` for
  build instructions).

## Setup

Add to your Neovim config:

```lua
require('vimficiency').setup()
```

If the library is not in a standard location, point at it with:

```bash
export VIMFICIENCY_LIB_PATH=/path/to/build/libvimficiency.so
```


Bindings must route through `vimfy.map()`, `<Plug>Vimfy*`, or `wrap()`;
see the keymap contract in the [Index](./README.md) and the full writeup
in [8. Keymaps](08-keymaps.md).

You can pass config overrides to `setup{}` — see
[9. Configuration](09-configuration.md).


## Verifying installation

```vim
:Vimfy help
```

If this prints the command list, you're ready.

If instead you see "command not defined", `setup()` didn't run — check your
config for syntax errors or a load-order issue, or see
[14. Troubleshooting](14-troubleshooting.md).

---

**[Index](./README.md)** | **[Next: Sessions →](02-sessions.md)**
