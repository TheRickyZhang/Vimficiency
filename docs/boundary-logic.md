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

== EditBoundary API (Simplified)

EditBoundary now uses raw chars instead of CharType enum:
```cpp
struct EditBoundary {
  char leftChar = NO_CHAR;   // Char before edit region ('\n' at line start, NO_CHAR at buffer start)
  char rightChar = NO_CHAR;  // Char after edit region ('\n' at line end, NO_CHAR at buffer end)
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
  QuoteFlags firstLineQuotes;   // For quote text object support
  QuoteFlags lastLineQuotes;
  BracketFlags firstLineBrackets;  // For bracket text object support
  BracketFlags lastLineBrackets;

  // Default constructor for manual setup
  EditBoundary() = default;

  // Construct from buffer context - analyzes chars around edit region
  EditBoundary(const Lines& lines, Position startPos, Position endPos);

  // Construct inheriting from parent boundary (for sub-regions)
  EditBoundary(const EditBoundary& parent, const Lines& lines, Position startPos, Position endPos);

  bool atLineEnd() const { return rightChar == '\n' || rightChar == NO_CHAR; }
  bool atLineStart() const { return leftChar == '\n' || leftChar == NO_CHAR; }
  bool isFullLineEditSafe() const { return atLineStart() && atLineEnd(); }
};
```

**Constructors**:
- Default: manual setup, fields default to NO_CHAR/false
- Primary: takes full buffer context, computes leftChar/rightChar from adjacent positions
- Inherited: starts with parent's boundary, refines based on new sub-region positions

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
dib, dab
```

= Paragraph Boundary Logic (Linewise)

Paragraphs are fundamentally different from words:
- **Linewise** vs characterwise
- **LineRange(startLine, endLine)** vs Range(Position, Position)
- Boundary is always a **blank line** (no crossing tables needed)

== Line Edge Types (Parallel to EdgeType)

```
LineEdgeType | Meaning
BlockEdge    | Edge of current same-type block (blank or non-blank lines)
GapEdge      | Edge of blank line run (adjacent to current paragraph)
NextEdge     | Start/end of next different-type block
```

Mapping to word EdgeType:
- BlockEdge ↔ WordEdge (edge of current unit)
- GapEdge ↔ GapEdge (edge of gap)
- NextEdge ↔ NextEdge (start/end of next unit)

== Paragraph Motion Commands

```
Motion | Line Edge Type
}      | (Forward, NextEdge)   - move to first blank line after paragraph
{      | (Backward, NextEdge)  - move to first blank line before paragraph
```

Unlike word motions, paragraph motions only have two variants (no e/ge equivalents).

== Paragraph Text Object Commands

```
Command |
dip  (Backward, BlockEdge) + (Forward, BlockEdge)
dap  {
  Cursor on non-blank line:
    Has trailing blank lines: (Backward, BlockEdge) + (Forward, GapEdge)
    Else: (Backward, GapEdge) + (Forward, BlockEdge)
  Cursor on blank line:
    (Backward, BlockEdge) + (Forward, NextEdge)
}
```

Note: The dap logic mirrors daw — same trailing/leading preference pattern.

== Paragraph Boundary Crossing

Unlike words (which need character-class based crossing tables), paragraphs use simple line comparison:

```
Forward:   endpointLine >= bottomBoundaryLine
Backward:  endpointLine <= topBoundaryLine
Text obj:  range.startLine <= topBoundary || range.endLine >= bottomBoundary
```

No crossing tables needed — blank lines are the only boundary type.

== API Parallel (Words vs Paragraphs)

```
Words (characterwise):
  motionWordEndpoint(cursor, lines, forward, EdgeType, big, skipCurrent) -> Position
  textObjectRange(cursor, lines, isInner, isBigWord) -> Range

Paragraphs (linewise):
  motionParagraphEdge(cursorLine, lines, forward, LineEdgeType) -> int
  paragraphTextObjectRange(cursorLine, lines, isInner) -> LineRange
```

The pattern is the same: compute endpoint → compare to boundary → decide if safe.

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
2. Place reserved boundary chars at edit region edges
3. Random cursor position within edit region
4. Execute motion, verify prefix/suffix intact via string matching
5. Only flag failure if: motion crossed but prediction said safe
   (Conservative predictions where we say "would cross" but motion
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

= Sentence Boundary Logic (Characterwise)

Sentences are **characterwise** like words, not linewise like paragraphs.
They have dual-source boundaries: punctuation patterns AND blank lines.

== Sentence Edge Types (Parallel to EdgeType)

```
SentenceEdgeType | Meaning
SentenceEdge     | Edge of current sentence (punctuation mark + closers)
GapEdge          | Edge of whitespace gap after sentence end
NextEdge         | Start of next sentence ()/( motions)
```

Mapping to word EdgeType:
- SentenceEdge ↔ WordEdge (edge of current unit)
- GapEdge ↔ GapEdge (edge of gap)
- NextEdge ↔ NextEdge (start/end of next unit)

== Sentence Boundary Detection

A sentence boundary is detected when:
1. **Punctuation pattern**: char is [.!?] AND followed by optional closers [)'"'\]]
   AND followed by (whitespace OR EOL)
2. **Blank line**: paragraph boundary = sentence boundary

```
Is char in [.!?]
  AND followed by zero or more of [)'"'\]]
  AND followed by (whitespace OR EOL)?
    → sentence end
OR is line blank?
    → sentence boundary
```

== Sentence Motion Commands

```
Motion | Sentence Edge Type
)      | (Forward, NextEdge)   - move to start of next sentence
(      | (Backward, NextEdge)  - move to start of previous sentence
```

Unlike words (which have w/e/b/ge), sentences only have two motion variants.

== Sentence Text Object Commands

```
Command |
dis  (Backward, SentenceEdge) + (Forward, SentenceEdge)
das  {
  Has trailing whitespace/blank lines:
    (Backward, SentenceEdge) + (Forward, GapEdge)
  Else (no trailing):
    (Backward, GapEdge) + (Forward, SentenceEdge)
}
```

Note: The das logic mirrors daw — same trailing/leading preference pattern.

== Sentence Boundary Crossing

Like words, sentences are characterwise, so we compare positions:

```
Forward:   endpoint >= rightBoundary
Backward:  endpoint <= leftBoundary
Text obj:  range.start <= leftBoundary || range.end >= rightBoundary
```

However, sentences have more complex boundary detection than words:
- Must track punctuation patterns, not just character classes
- Closers [)'"'\]] can extend the sentence end position
- Blank lines act as implicit sentence boundaries

== API Parallel (Sentences)

```
Sentences (characterwise, like words):
  motionSentenceEdge(cursor, lines, forward, SentenceEdgeType) -> Position
  sentenceTextObjectRange(cursor, lines, isInner) -> Range
```

The pattern matches words:
- `motionSentenceEdge` parallels `motionWordEndpoint`
- `sentenceTextObjectRange` parallels `textObjectRange`

== Edge Cases

=== Sentences with Closers

Example: `"Hello!" she said.`
- The `!` is the sentence end punctuation
- The `"` is a closer
- Sentence edge is at `"`
- The `.` starts a new sentence detection

=== Blank Lines as Sentence Boundaries

Blank lines act as both paragraph AND sentence boundaries:
- `}` from middle of paragraph → stops at blank line
- `)` from same position → also stops at blank line (if no sentence end found first)

=== Multiple Sentences on One Line

Example: `First. Second. Third.`
- Each `.` followed by space creates a sentence boundary
- `)` navigates between sentence starts
- `dis` selects from start to sentence-ending punctuation (+ closers)

=== Sentence at Buffer Start/End

- At buffer start: `(` stays at position
- At buffer end: `)` stays at position
- For `as` at buffer end with no trailing: include leading whitespace instead

= EditOptimizer Boundary Handling

EditOptimizer uses an **effectiveLines model** where boundary chars are baked into
the buffer content. This ensures cursor clamping after deletions matches the full
buffer behavior.

== Building effectiveLines

```
Input:  editRegion = {"hello", "world"}, leftChar='X', rightChar='Y'
Output: effectiveLines = {"Xhello", "worldY"}
        goalLines = {"XY"}  // empty edit region
```

For newline boundaries, empty lines are added instead:
```
Input:  editRegion = {"hello"}, leftChar='\n', rightChar='\n'
Output: effectiveLines = {"", "hello", ""}  // lineOffset=1
        goalLines = {"", "", ""}
```

== Limitations

The boundary char model has a known limitation with multi-line embedded regions.
When word motions cross into lines containing prefix/suffix content, word boundaries
may differ because effectiveLines only has a single boundary char, not the full
prefix/suffix content.

See `docs/edit-boundary-limitations.md` for detailed analysis.
