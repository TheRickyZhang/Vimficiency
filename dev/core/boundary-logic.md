# Boundary Logic

This document explains how boundaries constrain motion/edit searches in optimizers.

## Boundary Building Blocks

Paragraphs use direction-independent edge enums:

```
LineEdgeType (paragraphs): BlockEdge | GapEdge | NextEdge
```

- **BlockEdge**: Edge of current unit
- **GapEdge**: Edge of whitespace/blank gap after unit
- **NextEdge**: Start of next unit

Words and sentences expose explicit operations instead of shared edge enums.
Bare movement, operator ranges, and text objects share scanner landmarks but
are not interchangeable operations.

## Word Operation Mappings

### Word Motions
```
w, W  | WordMotionTarget::NextBegin
e, E  | WordMotionTarget::NextEnd
b, B  | WordMotionTarget::PreviousBegin
ge,gE | WordMotionTarget::PreviousEnd
```

### Word Deletions
```
dw,dW   | WordOperatorTarget::DeleteToNextWord
de,dE   | WordOperatorTarget::DeleteToWordEnd
db,dB   | WordOperatorTarget::DeleteBackToWordBegin
dge,dgE | WordOperatorTarget::DeleteBackToWordEnd
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
)   | pure movement target from `sentenceMotionEndpoint`
(   | pure movement target from `sentenceMotionEndpoint`
d)  | exclusive operator endpoint from `sentenceOperatorEndpoint`
d(  | exclusive operator endpoint from `sentenceOperatorEndpoint`
dis | sentence content range
das | sentence content plus trailing separator, or leading separator if needed
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

### NavBoundary

Wraps BoundaryContext for motion constraint checking (no string content needed):

```cpp
class NavBoundary {
  BoundaryContext ctx_;  // Delegates to shared context

  // Accessors delegate to ctx_
  bool hasLinesAbove() const { return ctx_.hasLinesAbove; }
  bool hasLinesBelow() const { return ctx_.hasLinesBelow; }
  int leftColOffset() const { return ctx_.leftColOffset; }
  int rightColOffset() const { return ctx_.rightColOffset; }

  // Conversion from TransformBoundary
  explicit NavBoundary(const TransformBoundary& eb);

  bool isPositionInBounds(pos, lastLine, lastLineLength) const;
};
```

### TransformBoundary

Full prefix/suffix strings for correct cursor clamping after line-merging deletions:

```cpp
struct TransformBoundary {
  std::string prefix_, suffix_;  // Content before/after edit region
  bool hasLinesAbove_, hasLinesBelow_;
  // + QuoteFlags/BracketFlags for text objects

  // Column offsets derived from string lengths
  int leftColOffset() const { return prefix_.size(); }
  int rightColOffset() const { return suffix_.size(); }

  // Convert to BoundaryContext for interop with NavBoundary
  BoundaryContext context() const;

  char leftChar() const;   // prefix_.back() or '\n'/NO_CHAR
  char rightChar() const;  // suffix_.front() or '\n'/NO_CHAR
  bool hasPrefix() const;
  bool hasSuffix() const;
};
```

### Why Two Boundary Types?

- **TransformBoundary needs strings**: Building `effectiveLines` requires prepending/appending
  actual content. Goal state comparison checks if lines match prefix+suffix.
- **NavBoundary needs only offsets**: Position bounds checking just needs column counts,
  not the actual characters. This keeps NavOptimizer lightweight.

The `BoundaryContext` struct extracts the shared logic (hasLinesAbove/Below computation,
offset storage) so both types compute boundaries consistently. Use `NavBoundary(eb)`
or `eb.context()` to convert when switching between optimizer types.

## Endpoint Functions (VimEndpointUtils.h)

Return sentinel values (`POSITION_OUTSIDE_BOUNDARY`, `LINE_OUTSIDE_BOUNDARY`, etc.) when crossing:

```cpp
// Words - semantic public operations
Position wordMotionEndpoint(cursor, lines, target, isBigWord, boundary);
Range wordOperatorRange(cursor, lines, target, isBigWord, boundary);
Range wordTextObjectRange(cursor, lines, kind, isBigWord, boundary);

// Paragraphs - templated on Forward and LineEdgeType
template<bool Forward, LineEdgeType Edge>
int motionParagraphEndpoint(cursorLine, lines, hasLinesOutside);
LineRange paragraphTextObjectRange(cursorLine, lines, isInner, topBoundary, bottomBoundary);

// Sentences - public operations stay semantically distinct
Position sentenceMotionEndpoint(cursor, lines, forward, boundaryOffset, hasLinesOutside);
Position sentenceOperatorEndpoint(cursor, lines, forward, boundaryOffset, hasLinesOutside);
Range sentenceTextObjectRange(cursor, lines, isInner, leftBoundary, rightBoundary);
```

Word operations expose semantic targets; their direction/edge scan details stay
inside VimCore. Paragraph endpoints still expose direction and `LineEdgeType`
because their call sites map directly to line-boundary categories.

Boundary inputs are family-specific:
- Words use `WordBoundaryContext`.
- Paragraphs use top/bottom line boundaries or `hasLinesOutside`.
- Sentences use column offsets plus `hasLinesOutside`.

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

Backward word operator ranges differ by operation:
- `db`/`dB`: exclude cursor char from deletion
- `dge`/`dgE`: include cursor char

**Critical**: Inclusive backward deletions must verify cursor position isn't in
boundary region, not just the motion endpoint.

## TransformOptimizer: effectiveLines Model

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

See `dev/edit-boundary-limitations.md` for known limitations with multi-line regions.

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
