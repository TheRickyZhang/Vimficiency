# Debug Taxonomy

This index clarifies what in `tests/Debug/` is actively maintained versus historical context.

## Active

| File | Build status | Purpose |
|------|--------------|---------|
| `tests/Debug/Debug.cpp` | Compiled into `vimficiency_debug` | Primary scratchpad for current investigations and repros. |
| `tests/Debug/DiffDebug.cpp` | Compiled into `vimficiency_diff_debug` | Standalone diff-region inspection tool. |

## Historical

| File | Build status | Purpose |
|------|--------------|---------|
| `tests/Debug/OldDebug.cpp` | Compiled into `vimficiency_debug` | Legacy investigations kept for regression forensics and reference. |
| `tests/Debug/Motion.cpp` | Reference-only | Archived motion/text-object investigation snippets. |
| `tests/Debug/MiscFailures.cpp` | Reference-only | Historical failure deep-dives and traces. |

## Archive

| File | Build status | Purpose |
|------|--------------|---------|
| `tests/Debug/Diffs.cpp` | Reference-only | Old diff-specific experiments superseded by `DiffDebug.cpp`. |
| `tests/Debug/Indent.cpp` | Reference-only | Old indentation/backspace investigation notes. |

## Usage Guidance

1. New ad-hoc debugging should go in `tests/Debug/Debug.cpp`.
2. Keep historical files as references unless they are actively reused.
3. If a reference-only file becomes useful again, promote it to Active and wire it into CMake explicitly.
