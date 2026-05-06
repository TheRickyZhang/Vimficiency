# Neovim vim.on_key Issues

This document describes known issues with `vim.on_key()` for capturing complete key sequences, particularly in operator-pending mode.

## Background

Vimficiency needs to capture the exact key sequence a user types to compare against optimal sequences. Neovim's `vim.on_key(callback)` API is the primary mechanism for this, but it has significant limitations.

```lua
vim.on_key(function(key, typed)
  -- key: the key after mappings are applied
  -- typed: the original key(s) before mapping expansion
end)
```

## Issue 1: Operator-Pending Duplication

### Symptom

When completing an operator-motion command, the motion key may fire twice:

| User Types | vim.on_key Records | Expected |
|------------|-------------------|----------|
| `cw`       | `c, w, w`         | `c, w`   |
| `dd`       | `d, d, d`         | `d, d`   |
| `yy`       | `y, y, y`         | `y, y`   |
| `d2w`      | `d, 2, w, w`      | `d, 2, w`|

### Cause

When Neovim processes an operator-pending command:
1. User presses `c` → `vim.on_key` fires, mode becomes operator-pending (`no`)
2. User presses `w` → `vim.on_key` fires (first time)
3. Operator completes, Neovim internally re-evaluates → `vim.on_key` fires again with `w`

The re-evaluation in step 3 causes the duplicate.

### Our Workaround

`build_sequence()` removes duplicates by detecting the live shape observed
from real captures:

- Same key appearing consecutively
- Both occurrences are in operator-pending mode (`no`)
- Keeping only the first occurrence already leaves the sequence ending in a
  complete operator edit (`cW`, `dw`, `dd`, etc.)

This keeps intentional repeated operator-pending keys such as `dgg`: `dg`
does not parse as a complete operator edit, so both `g` keys are retained.
It also keeps intentional typed text after a change, such as `cWW`: the
inserted `W` arrives with insert mode (`i`), not operator-pending mode.

Text-object prefixes have a second observed shape, where the duplicated
`i`/`a` prefix is followed by the object key (`d`, `no:i`, `n:i`, `n:w`).
The reducer drops the duplicate only when the sequence after dropping it
forms a complete operator edit (`diw`).

## Issue 2: Missing Text Object Keys

### Symptom

The final character of a text object specifier is never received by `vim.on_key`:

| User Types | vim.on_key Records | Expected |
|------------|-------------------|----------|
| `ciw`      | `c, i`            | `c, i, w`|
| `daw`      | `d, a`            | `d, a, w`|
| `yi"`      | `y, i`            | `y, i, "`|
| `ca(`      | `c, a`            | `c, a, (`|

### Cause

After typing the text object prefix (`i` or `a`), Neovim enters a sub-state waiting for the text object specifier. When that final key is pressed:
1. Neovim consumes it internally to determine the text object
2. The operation executes
3. `vim.on_key` never fires for that key

This is a fundamental limitation of how Neovim processes text objects.

### Mode During Text Object Input

```
After 'c':  mode = "no"  (operator-pending)
After 'i':  mode = "no"  (still operator-pending, waiting for specifier)
After 'w':  mode = "i"   (insert mode - operation completed, 'w' was never seen)
```

### No Current Workaround

This cannot be fixed within `vim.on_key`. Potential approaches:

#### A. Inference from Results (Heuristic)

Detect the pattern and infer the text object:
1. See `[cdyv]` followed by `[ia]` in operator-pending mode
2. Mode changes (operation completed)
3. Analyze what changed (deleted text range, cursor position)
4. Match against known text object behaviors

Challenges:
- Requires understanding all text object semantics
- Ambiguous cases (multiple text objects could produce same result)
- Complex to implement correctly

#### B. External Input Capture

Capture keys outside Neovim's processing:

1. **Input Proxy**: Run a process that intercepts stdin, logs keys, forwards to Neovim via RPC
   ```bash
   nvim --listen /tmp/nvim.sock &
   ./key_proxy /tmp/nvim.sock  # reads stdin, logs, forwards via nvim_input()
   ```

2. **System-Level Monitoring**:
   - Linux: `evdev` / `libinput` (requires input group permissions)
   - macOS: `CGEventTap` (requires accessibility permissions)
   - Windows: Low-level keyboard hooks

3. **Terminal Wrapper**: Custom terminal emulator or wrapper that logs all input

Challenges:
- Platform-specific implementations
- Permission requirements
- Synchronization between external capture and Neovim state
- Added latency and complexity

#### C. Neovim Feature Request

The cleanest solution would be Neovim exposing the motion/text-object used:

- **Issue**: [neovim/neovim#19426](https://github.com/neovim/neovim/issues/19426)
- **Request**: Expose `v:motion` variable (like `v:operator` exists)
- **Status**: Open feature request (as of 2024)

If implemented, we could use:
```lua
vim.on_key(function(key, typed)
  -- After operation completes, check v:motion for the full motion/text-object
  local motion = vim.v.motion  -- hypothetical
end)
```

## Alternative Approach: Macro Recording

We previously tried using Vim's built-in macro recording (`q{register}`):

```lua
-- Start recording
vim.cmd('normal! qz')

-- ... user edits ...

-- Stop and retrieve
vim.cmd('normal! q')
local sequence = vim.fn.getreg('z')
```

### Advantages
- Captures complete sequences including text objects
- Uses Vim's proven mechanism

### Disadvantages
- Records the stop command itself (`:Vimfy end a<CR>`)
- Shows "recording @z" in statusline
- Blocks user from recording their own macros during session
- Converts `<leader>` to literal key (e.g., space)

We reverted from this approach due to these usability issues.

## Current State

| Issue | Status | Workaround |
|-------|--------|------------|
| Operator-pending duplication | ✅ Fixed | Mode-based deduplication in `build_sequence()` |
| Text object missing key | ⚠️ Partial | Explore uses physical snapshots; global capture can still miss it |

## Impact on Vimficiency

For sequences with text objects (`ciw`, `daw`, `yi"`, etc.):
- Global capture may record an incomplete user sequence, so cost comparison can be inaccurate
- Explore phase/buffer transitions do not depend on a perfect `on_key` stream
- Optimizer suggestions are still generated independently

## Binding-shape characterization (2026-04)

Recorded behavior of `vim.on_key` when the user types the LHS of a
multi-key Normal-mode mapping. Pinned down by
`tests/lua/capture/on_key_mapping_probe.lua`. **Two distinct paths
depending on how the LHS is delivered:**

### Path A — LHS delivered as one burst (no delay between keys)

What happens when `nvim_feedkeys("<Space>ve", ...)` is called — all
three bytes land in the typeahead queue together, and nvim resolves
the mapping in a single input pass with no pending-state excursion.

| Binding shape                 | Events | `typed`         | `key`                   | Dedup rule drops it? |
|-------------------------------|--------|-----------------|-------------------------|----------------------|
| Lua callback (`vimfy.map`)    | 1      | full LHS        | Lua-callback sentinel   | Yes                  |
| String RHS (`"<Nop>"`, Ex)    | 1      | full LHS        | first expansion key     | Yes                  |
| `<Plug>`-remapped             | 1      | full LHS        | Lua-callback sentinel   | Yes                  |
| Unbound (literal keystrokes)  | N      | key per event   | equal to `typed`        | No — recorded        |

### Path B — LHS delivered key-by-key in real time (>0ms between keys)

What actually happens at human typing speed — each keystroke is a
separate input event, so nvim processes them one at a time and has to
wait for the next key before knowing whether the mapping resolves.
During that wait, nvim fires on_key for each pending key **in
addition to** the eventual resolution event. Observed via ad-hoc
`vim.on_key` instrumentation on a live `<Space>ve` mapping:

```
typed="<Space>"   key="<t_...>"     ← pending event 1 (single byte, typed ~= key)
typed="v"         key="v"           ← pending event 2 (typed == key)
typed="e"         key="e"           ← pending event 3 (typed == key)
typed="<Space>ve" key="<t_...>"     ← resolution event (#typed > 1, typed ~= key)
```

**Four events fire, not one.** The resolution event is still caught by
`#typed > 1 and typed ~= key`, but the three pending events have
`#typed == 1` and slip through — they get recorded into
`session.key_seq`, leaking the mapping's LHS into the captured motion
stream.

### Fix: retroactive strip on resolution

The per-session on_key handler and the global on_key handler both
implement retroactive strip when the resolution event fires. Walk
back over `session.key_seq` (or every active recall record's
`key_seq` for the global path via `session_store.strip_recall_pre_resolution`),
concatenate `key_typed_raw` bytes, and pop the tail if it matches
the resolution's `typed`. Mismatch (e.g., the record's tail has been
rotated out, or the pending events went to a different window and
weren't recorded) is silently skipped — strip is best-effort.

### Residual cases where LHS bytes can still appear in captured motion

- **Partial LHS, no resolution** — user types `<Space>v` then presses
  `<Esc>` or waits past `timeoutlen`. No resolution event fires; the
  pending events stay in `key_seq`. Matches what the user actually
  typed; not a bug.
- **Binding not loaded yet** — `<Space>ve` bound after the session
  started capturing. No mapping, path A's "unbound" row applies.
- **Buffer-local binding in the wrong buffer** — mapping doesn't
  match, same as unbound.
- **Prefix-only binding** — only `<Space>` is bound; `<Space>ve`
  isn't. nvim resolves `<Space>` and releases `v`, `e` as
  independent motions.

## References

- [neovim/neovim#15527](https://github.com/neovim/neovim/issues/15527) - vim.on_key behavior discussion
- [neovim/neovim#19426](https://github.com/neovim/neovim/issues/19426) - v:motion feature request
- `:help vim.on_key()` - Official documentation
- `:help operator-pending` - Operator-pending mode explanation

## Future Work

1. **Monitor Neovim #19426** for `v:motion` implementation
2. **Consider inference approach** for common text objects
3. **Evaluate external capture** if high accuracy is required
4. **Document limitation** to users when text objects are detected
