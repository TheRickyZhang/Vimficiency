# Counted Edit Semantics: `{n}{edit}` vs `{edit}` repeated n times

When optimizing edit sequences, we might want to collapse `dd...` (dd + 3 dots) into `4dd`. This requires `{n}{edit}` to produce the **exact same buffer and cursor state** as applying `{edit}` n times sequentially (with dot repeat). This document catalogs every edit command's count behavior.

## The Core Distinction

Vim has two fundamentally different count semantics:

1. **Repeat count**: The command is literally executed n times in sequence. The cursor repositions between each execution.
2. **Scope count**: The count changes what the single operation affects (e.g., how many lines, how far a motion reaches).

Most Vim commands use **scope count**, making `{n}{edit}` ≠ `{edit}` repeated n times.

## Command-by-Command Analysis

### Character Operations

| Command | Count meaning | `{n}{edit}` = `{edit}×n`? | Notes |
|---------|--------------|---------------------------|-------|
| `x` | Delete n chars forward from cursor | **No** | `4x` deletes 4 chars in one shot (clamped to line end). `xxxx` repositions cursor between each delete — can wrap when near EOL, deleting backward after clamping. Differ near line end. |
| `X` | Delete n chars backward | **No** | Same issue as `x` but backward. `4X` clamped to chars before cursor. |
| `~` | Toggle n chars and advance | **No** | `4~` toggles 4 chars in one shot (clamped to line end). `~~~~` repositions each time — can toggle the same char multiple times at EOL. |

**Key insight**: Even single-character operations differ near line boundaries because the count version clamps to available characters, while sequential repetition repositions the cursor between each execution.

### Line Operations

| Command | Count meaning | `{n}{edit}` = `{edit}×n`? | Notes |
|---------|--------------|---------------------------|-------|
| `dd` | Delete n contiguous lines from cursor | **No** | `4dd` deletes lines `[cursor, cursor+3]` at once. `dd` repeated 4 times deletes line at cursor each time — always the same line number as lines shift up. Equivalent when enough lines exist below, but `dd×4` can delete MORE lines total when the cursor clamps after running out of lines below (subsequent `dd`s delete lines above the original range). |
| `J` | Join n+1 lines (cursor line + n below) | **No** | `4J` joins current + 3 below = 4 lines into one. `JJJJ` joins 2 lines 4 times = 5 lines joined. The count semantics are completely different: `[count]J` means "join count lines" (`:help J`). |
| `gJ` | Same as J without space | **No** | Same count semantics as `J`. |

### Operator + Motion Deletions

For `d{motion}` commands, `{n}d{motion}` is parsed as `d{n}{motion}` — the count applies to the **motion**, changing how far it reaches. This is fundamentally different from repeating `d{motion}` n times.

| Command | `{n}{edit}` meaning | `{edit}×n` meaning | Equivalent? |
|---------|--------------------|--------------------|-------------|
| `de`/`dE` | `d{n}e`: delete to nth word-end from cursor | Delete to next word-end, n times (cursor repositions) | **No** — `4de` deletes a contiguous range to the 4th word-end; `de...` deletes 4 separate word-ends, each range starting from the repositioned cursor |
| `dw`/`dW` | `d{n}w`: delete to nth word-begin | Delete to next word-begin, n times | **No** — same reason |
| `db`/`dB` | `d{n}b`: delete back to nth word-begin | Delete back to prev word-begin, n times | **No** |
| `dge`/`dgE` | `d{n}ge`: delete back to nth word-end | Delete back to prev word-end, n times | **No** |
| `D`/`d$` | `{n}D`: delete from cursor through n-1 lines below | Delete to EOL, n times | **No** — `4D` spans 4 lines; `D...` deletes to EOL on the current line then the next, etc. |
| `d0` | Count is ignored by Vim | — | N/A |
| `d}`/`d{`/`d)`/`d(` | Motion reaches nth paragraph/sentence boundary | Repeated paragraph/sentence deletes | **No** |
| `diw`/`daw`/etc. | Text object doesn't use count | — | N/A |

**Concrete example showing the difference:**

```
Buffer: "one two three four five"
Cursor at col 0

     Result: " five"
4de: d4e → delete from col 0 to 4th word-end (col 16, 'r' of "four")

de...: de deletes "one" → " two three four five", cursor at 0
       de deletes " two" → " three four five", cursor at 0
       de deletes " three" → " four five", cursor at 0
       de deletes " four" → " five", cursor at 0
       Result: " five"
```

Wait — in this case they produce the same result! But this is coincidental. When content has varying word lengths or the cursor isn't at position 0, they diverge:

```
Buffer: "ab cdef gh ij"
Cursor at col 1 ('b')

2de: d2e → delete from col 1 to 2nd word-end (col 7, 'f')
     Result: "a gh ij"

de.: de deletes from col 1 to col 3 ('d'), → "aef gh ij", cursor at 1
     de deletes from col 1 to col 2 ('f'), → "a gh ij", cursor at 1
     Result: "a gh ij"
```

They happen to match here too because `de` from a word-internal position deletes to word-end, then the next `de` starts fresh. But consider cross-line cases where `d{n}e` stays characterwise while `de` repeated can trigger different boundary behaviors.

## Implications for the EditOptimizer

### Current approach: Dot repeat (safe)

The EditOptimizer currently emits `{edit}.` sequences (e.g., `dd..`). This is always correct because dot repeat literally re-executes the command from the current cursor position.

### Post-hoc collapse: Unsafe

Collapsing `dd...` → `4dd` is unsafe for all commands listed above. The only hypothetically safe collapses would require proving that the specific buffer state makes them equivalent — too expensive to verify post-hoc.

### Proper counted edit generation

To emit counted edits correctly, the EditOptimizer must generate them **directly during search**, computing the actual result of `{n}{edit}` rather than assuming it equals `{edit}` repeated n times. This means:

1. **Line deletions** (`dd`): Given current buffer, compute `min(n, lines_remaining_below + 1)` lines deleted. The result of `{n}dd` is always deleting contiguous lines `[cursor, cursor+n-1]` (clamped to buffer end).

2. **Line joins** (`J`, `gJ`): `{n}J` joins n lines (current + n-1 below). Must have n-1 lines below cursor. Result is a single merge of n lines, with spaces inserted between each (for `J`).

3. **Word-motion deletions** (`de`, `dw`, etc.): `{n}de` = `d{n}e` — apply the motion n times to find the endpoint, then delete the range `[cursor, endpoint]` in one operation. This is exactly how `Edit.cpp` already implements it (loop the motion n times, delete once).

4. **`D`/`d$`**: `{n}D` deletes from cursor through n-1 lines below. Range is `[cursor, (cursor.line+n-1, lastCol)]`.

### Priority for implementation

| Approach | Complexity | Value |
|----------|-----------|-------|
| `{n}dd` | Low — count full lines remaining | High — common pattern |
| `{n}J` / `{n}gJ` | Low — count lines below | Medium |
| `{n}de` / `{n}dw` etc. | Medium — simulate motion n times | Medium |
| `{n}D` | Low — count lines below | Low — less common |

See also: `vim-edge-cases.md` for d vs c operator differences that affect the delete→change conversion when counted edits reach the goal state.
