# Utility Patterns


General-purpose utilities in `src/Utils/`.


## Lines (`Lines.h`)

Wrapper around `vector<string>` with position-aware character access. Key methods:
- `get(pos)` - returns char at position, `'\n'` for empty lines
- `getNextPos()` / `getPrevPos()` - step to adjacent char (skip empty lines)
- `getNextPosIncludeEmpty()` / `getPrevPosIncludeEmpty()` - step including empty lines
- `hashLines(lines)` - FNV-1a hash over all line contents + line count. Used in `EditStateKey` and `SuffixKey` to avoid full buffer copies in A* costmap keys (see `edit-optimizer.md` § State Hashing).

See `boundary-logic.md` for when to use each variant.

## StringUtils (`StringUtils.h`)

String manipulation helpers, including `flattenLines()` for buffer-to-string conversion used in edit distance analysis.

## CommandSequence (`State/CommandSequence.h`)

String wrapper for Vim command sequences with human-readable formatting. Inherits from `std::string` (like `Line`).

```cpp
struct CommandSequence : std::string {
  using std::string::string;
  CommandSequence(const std::string& s) : std::string(s) {}

  // Format for display: tokenize into logical units and join with spaces
  // e.g., "3rx<C-d>ciwfoo<Esc>" -> "3rx <C-d> ciw foo <Esc>"
  std::string formatted() const;

  // Pretty-print via operator<<
  friend std::ostream& operator<<(std::ostream& os, const CommandSequence& cs);
};

// Standalone utility for formatting any sequence string
std::string formatSequenceForDisplay(const std::string& seq);
```

**Tokenization**: Uses `parseSequenceStrings()` from `SequenceParser.h` which splits sequences into:
- Motions: `w`, `3j`, `fa;`, `<C-d>`
- Edits/Changes: `ciw`, `dd`, `D`, `s`
- Typed text: Characters typed in insert mode (between change command and `<Esc>`)
- Special keys: `<Esc>`

**Usage**: FFI output and debug display. Raw sequences are used internally for execution and cost calculation.

## Debug (`Debug.h`)

`debug()` macro for conditional debug output. Enabled when `VIMFICIENCY_DEBUG=true` (default in debug builds).
