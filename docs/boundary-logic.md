# Boundary Logic

This document explains how boundaries constrain motion/edit searches in optimizers.

## Edge Types (Building Blocks)

Three parallel enums define where operations stop:

```
EdgeType (words):          WordEdge | GapEdge | NextEdge
LineEdgeType (paragraphs): BlockEdge | GapEdge | NextEdge
SentenceEdgeType:          SentenceEdge | GapEdge | NextEdge
```

- **WordEdge/BlockEdge/SentenceEdge**: Edge of current unit
- **GapEdge**: Edge of whitespace/blank gap after unit
- **NextEdge**: Start of next unit

These are direction-independent. See `MotionToSpec.h` and `EditToSpec.h` for full specs.

## Command → Edge Type Mappings

### Word Motions
```
w, W  | (Forward, NextEdge)   - start of next word
e, E  | (Forward, WordEdge)   - end of current/next word (skipCurrent=true)
b, B  | (Backward, WordEdge)  - start of previous word (skipCurrent=true)
ge,gE | (Backward, NextEdge)  - end of previous word
```

### Word Deletions
```
dw,dW  | (Forward, GapEdge)   - delete to gap edge
de,dE  | (Forward, WordEdge)  - delete to word end (skipCurrent=true)
db,dB  | (Backward, WordEdge) - delete to word start (skipCurrent=true, isExclusiveAtCursor=true)
dge,dgE| (Backward, NextEdge) - delete to previous word end
```

### Word Text Objects
```
diw,diW | (Backward, WordEdge) + (Forward, WordEdge)
daw,daW | Trailing whitespace? (Backward, WordEdge) + (Forward, GapEdge)
        | Else:                (Backward, GapEdge) + (Forward, WordEdge)
```

### Paragraph Motions/Deletions
```
}   | (Forward, NextEdge)  - first blank line after paragraph
{   | (Backward, NextEdge) - first blank line before paragraph
dip | (Backward, BlockEdge) + (Forward, BlockEdge)
dap | Similar trailing/leading logic as daw
```

### Sentence Motions/Deletions
```
)   | (Forward, NextEdge)  - start of next sentence
(   | (Backward, NextEdge) - start of previous sentence
dis | (Backward, SentenceEdge) + (Forward, SentenceEdge)
das | Similar trailing/leading logic as daw
```

## Why Boundaries?

When optimizing edits on a sub-buffer, motions must not escape into prefix/suffix
content. Boundary structs encode what's outside so endpoint functions can reject
crossing motions.

## Boundary Structs

Three related structs handle boundary information at different levels of detail:

### BoundaryContext (Shared Foundation)

Lightweight struct with the core boundary data shared by both boundary types:

```cpp
struct BoundaryContext {
  bool hasLinesAbove = false;   // Lines exist above edit region
  bool hasLinesBelow = false;   // Lines exist below edit region
  int leftColOffset = 0;        // Forbidden columns at line 0 start
  int rightColOffset = 0;       // Forbidden columns at last line end

  // Two constructors: from parent context, or from explicit bool flags (FFI)
  BoundaryContext(lines, firstPos, lastPos, parent);
  BoundaryContext(lines, firstPos, lastPos, hasLinesAbove, hasLinesBelow);
};
```

### MotionBoundary

Wraps BoundaryContext for motion constraint checking (no string content needed):

```cpp
class MotionBoundary {
  BoundaryContext ctx_;  // Delegates to shared context

  // Accessors delegate to ctx_
  bool hasLinesAbove() const { return ctx_.hasLinesAbove; }
  bool hasLinesBelow() const { return ctx_.hasLinesBelow; }
  int leftColOffset() const { return ctx_.leftColOffset; }
  int rightColOffset() const { return ctx_.rightColOffset; }

  // Conversion from EditBoundary
  explicit MotionBoundary(const EditBoundary& eb);

  bool isPositionInBounds(pos, lastLine, lastLineLength) const;
};
```

### EditBoundary

Full prefix/suffix strings for correct cursor clamping after line-merging deletions:

```cpp
struct EditBoundary {
  std::string prefix_, suffix_;  // Content before/after edit region
  bool hasLinesAbove_, hasLinesBelow_;
  // + QuoteFlags/BracketFlags for text objects

  // Column offsets derived from string lengths
  int leftColOffset() const { return prefix_.size(); }
  int rightColOffset() const { return suffix_.size(); }

  // Convert to BoundaryContext for interop with MotionBoundary
  BoundaryContext context() const;

  char leftChar() const;   // prefix_.back() or '\n'/NO_CHAR
  char rightChar() const;  // suffix_.front() or '\n'/NO_CHAR
  bool hasPrefix() const;
  bool hasSuffix() const;
};
```

### Why Two Boundary Types?

- **EditBoundary needs strings**: Building `effectiveLines` requires prepending/appending
  actual content. Goal state comparison checks if lines match prefix+suffix.
- **MotionBoundary needs only offsets**: Position bounds checking just needs column counts,
  not the actual characters. This keeps MotionOptimizer lightweight.

The `BoundaryContext` struct extracts the shared logic (hasLinesAbove/Below computation,
offset storage) so both types compute boundaries consistently. Use `MotionBoundary(eb)`
or `eb.context()` to convert when switching between optimizer types.

## Endpoint Functions (VimEndpointUtils.h)

Return sentinel values (`POSITION_OUTSIDE_BOUNDARY`, etc.) when crossing:

```cpp
// Words
Position motionWordEndpoint(cursor, lines, forward, EdgeType, big, skipCurrent,
                            boundaryOffset, hasLinesOutside);
Range textObjectRange(cursor, lines, isInner, isBigWord,
                      leftColOffset, rightColOffset, hasLinesAbove, hasLinesBelow);

// Paragraphs
int motionParagraphEndpoint(cursorLine, lines, forward, LineEdgeType, boundaryLine);
LineRange paragraphTextObjectRange(cursorLine, lines, isInner, topBoundary, bottomBoundary);

// Sentences
Position motionSentenceEndpoint(cursor, lines, forward, SentenceEdgeType, boundary);
Range sentenceTextObjectRange(cursor, lines, isInner, leftBoundary, rightBoundary);
```

## Crossing Tables (Conceptual)

Word motions check `(contentEdgeChar, boundaryChar)` pairs. Motion is **safe** when
the table returns `no`. Example for WordEdge:

```
              | bc=Keyword | bc=Whitespace | bc=Symbol | bc=Newline
--------------+------------+---------------+-----------+-----------
char=Keyword  | YES        | no            | no        | no
char=Space    | YES        | YES           | YES       | no
char=Symbol   | no         | no            | YES       | no
```

Key insight: Newlines always block crossing (rightmost column all `no`).
This is why `hasLinesAbove`/`hasLinesBelow` gates vertical escape.

For full tables, see `wouldCross*` functions in `VimEndpointUtils.cpp`.

## Notable Edit Behaviors

From `EditToSpec.h`, `BackwardWordEditSpec` has `isExclusiveAtCursor`:
- `db`/`dB`: `true` — excludes cursor char from deletion
- `dge`/`dgE`: `false` — includes cursor char

**Critical**: Inclusive backward deletions must verify cursor position isn't in
boundary region, not just the motion endpoint.

## EditOptimizer: effectiveLines Model

Prepends prefix, appends suffix to create `effectiveLines`:

```
editRegion = {"hello", "world"}, prefix = "XX", suffix = "YY"
→ effectiveLines = {"XXhello", "worldYY"}, goalLines = {"XXYY"}
  leftColOffset = 2, rightColOffset = 2
```

For empty prefix/suffix with `hasLinesAbove`/`Below`, empty lines are added:
```
editRegion = {"hello"}, hasLinesAbove=true, hasLinesBelow=true
→ effectiveLines = {"", "hello", ""}, lineOffset = 1
```

See `docs/edit-boundary-limitations.md` for known limitations with multi-line regions.

## Key Implementation Notes

### skipCurrent Behavior
- `db`/`dB`, `de`/`dE`: start search from adjacent char, not cursor
- When predicting crossing: check `(charBeforeCursor, leftBoundary)` for backward,
  `(charAfterCursor, rightBoundary)` for forward
- Newlines are transparent for adjacent char lookup (skip to prev/next line)

### Empty Lines Are Words
Per Vim docs: "An empty line is also considered to be a word."
- `Lines::get()` returns `'\n'` for empty lines
- `w` before empty line → stops AT the empty line
- `e` before empty line → stops at word end BEFORE empty line (no "end" on empty)

### Character Classification
- `isWhitespace(c)`: space/tab only — for within-line blank skipping
- `isBlank(c)`: space/tab/newline — for general blank checks
- Use `isWhitespace` to avoid incorrectly skipping past empty lines

### Sentence Boundaries
Detected when: char in `[.!?]` + optional closers `[)'"'\]]` + whitespace/EOL.
Blank lines also act as sentence boundaries (paragraph boundary = sentence boundary).
