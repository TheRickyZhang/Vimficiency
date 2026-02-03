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

When completing an operator-motion command, the motion key fires twice:

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

In `key_tracking.lua`, the `build_sequence()` function removes duplicates by detecting:
- Same key appearing consecutively
- First occurrence in operator-pending mode (`no`)
- Second occurrence in a different mode (normal `n` or insert `i`)

```lua
if same_key and curr_mode == "no" and next_mode ~= "no" then
  -- Keep current, skip next (the duplicate)
  i = i + 2
end
```

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
| Text object missing key | ❌ Unfixed | None currently implemented |

## Impact on Vimficiency

For sequences with text objects (`ciw`, `daw`, `yi"`, etc.):
- User sequence is recorded incompletely
- Cost comparison may be inaccurate
- Optimizer suggestions are still valid (generated independently)

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
