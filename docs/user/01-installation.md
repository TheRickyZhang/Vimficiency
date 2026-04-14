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


Any key you bind to a Vimfy action must announce itself as admin
activity; otherwise the invoking keystroke is counted as motion. Route
bindings through `vimfy.map()`, a `<Plug>Vimfy*` map, or `wrap()` — see
[8. Keymaps](08-keymaps.md).

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
