= Boundary Logic

== Edge Types (Core Abstraction)

All word operations find an *edge* - where the operation stops. Three types:
WordEdge: Edge of the word we traverse (step back into the word)
GapEdge:  Edge of the gap before next word (step back into gap)
NextEdge: Edge of the next unit (stay at first char of next thing)

These edge types are DIRECTION-INDEPENDENT. The physical position depends
on the direction of travel, but the concept is the same regardless of direction.

== Vim Motion Commands

Motion commands move the cursor without modifying the buffer.

=== word motions
```
Motion | Edge Type
w      | (Forward, NextEdge)   - move to START of next word
e      | (Forward, WordEdge)   - move to END of current/next word
b      | (Backward, WordEdge)  - move to START of previous word
ge     | (Backward, NextEdge)  - move to END of previous word
```

=== WORD motions
```
Motion | Edge Type
W      | (Forward, NextEdge)   - move to START of next WORD
E      | (Forward, WordEdge)   - move to END of current/next WORD
B      | (Backward, WordEdge)  - move to START of previous WORD
gE     | (Backward, NextEdge)  - move to END of previous WORD
```

Note: `e`/`E` and `b`/`B` and `ge`/`gE` need to skip current position first
(skipCurrent=true), otherwise they would stay at the current word boundary.

== Vim Deletion Commands

A deletion operation from currCol to edge is INCLUSIVE.

=== word deletions
Here, you can see that de/db are symmetric, and vim only gives us a few combinations compared to all possibilities.
```
Command |
de   Current Char + (Forward, WordEdge) from next char
db   Current Char + (Backward, WordEdge) from prev char
dw   (Forward, GapEdge)
dge  (Backward, NextEdge)
```

=== WORD deletions
```
Command |
dE   Current Char + (Forward, WordEdge) from next char
dB   Current Char + (Backward, WordEdge) from prev char
dW   (Forward, GapEdge)
dgE  (Backward, NextEdge)
```

== Crossing Tables

Each edge type has a crossing table. Motion is *safe* when table returns `no`.

=== WordEdge
```
              |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
--------------+--------------+-----------------+-------------+--------------+
char=Keyword  |  YES         |  no             |  no         |  no          |
char=Space    |  YES         |  YES            |  YES        |  no          |
char=Symbol   |  no          |  no             |  YES        |  no          |
```
Note: Whitespace isn't a word, so `e` from whitespace goes to NEXT word end, crossing everything.
Symbol row mirrors Keyword row (wordChar/nonWordChar symmetry).

=== GapEdge
```
              |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
--------------+--------------+-----------------+-------------+--------------+
char=Keyword  |  YES         |  YES            |  no         |  no          |
char=Space    |  no          |  YES            |  no         |  no          |
char=Symbol   |  no          |  YES            |  YES        |  no          |
```
Note: Symbol row mirrors Keyword row (wordChar/nonWordChar symmetry).

=== NextEdge
```
              |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
--------------+--------------+-----------------+-------------+--------------+
char=Keyword  |  YES         |  YES            |  YES        |  no          |
char=Space    |  YES         |  YES            |  YES        |  no          |
char=Symbol   |  YES         |  YES            |  YES        |  no          |
```
Note: `ge` always goes to previous word end, regardless of current content type.

=== LineEdge
```
              |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
--------------+--------------+-----------------+-------------+--------------+
              |  YES         |  YES            |  YES        |  no          |
```


== Applying Crossing Checks
- *Forward*: check `(lastChar, rightBoundary)` using the edge's table
- *Backward*: check `(firstChar, leftBoundary)` using the edge's table

Where:
- `lastChar` / `firstChar` = char at the edge of current content
- `rightBoundary` / `leftBoundary` = char just OUTSIDE the edit region

== Text Object Commands
```
Command |
diw  (Backward, WordEdge) + (Forward, WordEdge)
daw  {
  Cursor in word/sentence word:
    Has trailing whitespace/newline: (Backward, WordEdge) + (Forward, GapEdge)
    Else: (Backward, GapEdge) + (Forward, WordEdge)
  Cursor in whitespace:
    (Backward, GapEdge) + (Forward, WordEdge)
}
```

== WORD Variants
Same edge types, but Keyword and Symbol merge into "NonWS" class.

=== WordEdge (WORD)
```
              |  bc=NonWS  |  bc=Whitespace  |  bc=Newline  |
--------------+------------+-----------------+--------------+
char=WORD     |  YES       |  no             |  no          |
char=Space    |  YES       |  YES            |  no          |
```
Note: Whitespace isn't a WORD, so `E` from whitespace goes to NEXT WORD end.

=== GapEdge (WORD)
```
              |  bc=NonWS  |  bc=Whitespace  |  bc=Newline  |
--------------+------------+-----------------+--------------+
char=WORD     |  YES       |  YES            |  no          |
char=Space    |  no        |  YES            |  no          |
```

=== NextEdge (WORD)
```
              |  bc=NonWS  |  bc=Whitespace  |  bc=Newline  |
--------------+------------+-----------------+--------------+
char=WORD     |  YES       |  YES            |  no          |
char=Space    |  YES       |  YES            |  no          |
```

== Text Object Commands (WORD)
```
Command
diW  (Backward, WordEdge) + (Forward, WordEdge)
daW  {
  Cursor in word/sentence word:
    Has trailing whitespace/newline: (Backward, WordEdge) + (Forward, GapEdge)
    Else: (Backward, GapEdge) + (Forward, WordEdge)
  Cursor in whitespace:
    (Backward, GapEdge) + (Forward, WordEdge)
}
```

== Other commands (TODO in future)
```
dd   (LineEdge)
dip, dap
dib, dab
```

== Testing Considerations (BoundaryTest)

=== db/de/dB/dE Check the NEXT Char, Not Current

These commands have exclusive behavior on the current char:
- `db`: does NOT delete current char; starts WordEdge search from previous char
- `de`: deletes current char, then starts WordEdge search from next char
- `dB`/`dE`: same pattern for WORD variants

When predicting crossing:
- `db`/`dB`: check `(charBeforeCursor, leftBoundary)`
- `de`/`dE`: check `(charAfterCursor, rightBoundary)`
- `dw`/`dW`/`dge`/`dgE`: check `(contentEdgeChar, boundary)`

=== Newlines Are Transparent for Adjacent Char Lookup

When finding the "previous char" or "next char" for db/de, skip newlines.
Motions traverse across lines, so the adjacent char is the last/first
non-newline char on the adjacent line.

Example:
```
Line 0: "hello "
Line 1: "world"
Cursor at 'w' (line 1, col 0)
```
- `charBeforeCursor` = ' ' (space at end of line 0), NOT Newline
- `db` from 'w' searches backward from the space, not from newline

=== Random Buffer Stress Test Design

The stress test verifies crossing predictions against Neovim:
1. Generate random buffer content
2. Place reserved boundary chars (one per CharType) at edit region edges
3. Random cursor position within edit region
4. Execute motion, verify prefix/suffix intact via string matching
5. Only flag failure if: motion crossed but crossFn predicted safe
   (Conservative predictions where crossFn says "would cross" but motion
   didn't reach boundary are acceptable)

== Critical Edge Cases

=== Empty Lines Are Words

Per vim docs: "An empty line is also considered to be a word."

This affects motion behavior:
- `w` from end of line before empty line → stops AT the empty line (line N+1, col 0)
- `b` from start of line after empty line → stops AT the empty line
- `e` behavior: empty line has no "end", so `e` stops at end of word BEFORE empty line
- `ge` behavior: similarly stops at end of word before empty line

Implementation notes:
- `Lines::get()` returns `'\n'` for empty lines (col 0 of empty line)
- Use `isWhitespace()` (space/tab only) for within-line blank skipping
- Use `isBlank()` (includes newline) for general blank checks
- After `skipCurrent` lands on empty line, return immediately for `b`/`B` (WordEdge)

=== Line Crossing Is a Word Boundary

Newlines terminate words in BOTH directions. When traversing characters:
- Forward: crossing to next line = word boundary, then skip leading whitespace
- Backward: crossing to previous line = word boundary, step back to word start

Key implementation detail: update character `c` BEFORE checking line crossing,
so Phase 3/4 have the correct character for the new line:
```cpp
c = lines.get(pos);  // Update BEFORE line check
if (pos.line != prev.line) break;
```

=== Character Stepping With Empty Lines

The `Lines` class has two stepping modes:
- `getNextPos()`/`getPrevPos()`: Skip empty lines (for char-by-char traversal)
- `getNextPosIncludeEmpty()`/`getPrevPosIncludeEmpty()`: Include empty lines

Word motions use the "IncludeEmpty" variants because empty lines are words.
Other operations (like find char `f`/`t`) may use the skipping variants.

=== Whitespace vs Blank

Two character classification functions:
- `isWhitespace(c)`: space or tab only - for skipping within lines
- `isBlank(c)`: space, tab, or newline - for general blank checks

Use `isWhitespace` in Phase 4 (skip blanks to next word) to avoid
incorrectly skipping past empty lines.
