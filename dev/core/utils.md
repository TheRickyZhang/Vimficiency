# Utility Patterns


General-purpose utilities in `src/Utils/`.


## Lines (`Lines.h`)

Wrapper around `vector<string>` with position-aware character access. Key methods:
- `get(pos)` - returns char at position, `'\n'` for empty lines
- `getNextPos()` / `getPrevPos()` - step to adjacent char (skip empty lines)
- `getNextPosIncludeEmpty()` / `getPrevPosIncludeEmpty()` - step including empty lines
- `hashLines(lines)` - FNV-1a hash over all line contents + line count. Stored on `TransformEditorState` and read by `TransformStateKey`/`SuffixKey` to avoid full buffer copies in A* costmap keys (see `dev/optimizer/transform-optimizer.md` § State Hashing).

See `boundary-logic.md` for when to use each variant.

## StringUtils (`StringUtils.h`)

String manipulation helpers, including `flattenLines()` for buffer-to-string conversion used in edit distance analysis.

## Sequence Formatting (`Interpreter/SequenceFormatting.h`)

Formatting utility for Vim command sequence display:

```cpp
std::string formatSequenceForDisplay(std::string_view seq);
```

**Tokenization**: Uses `parseSequenceStrings()` from `SequenceParser.h` which splits sequences into:
- Motions: `w`, `3j`, `fa;`, `<C-d>`
- Edits/Changes: `ciw`, `dd`, `D`, `s`
- Typed text: Characters typed in insert mode (between change command and `<Esc>`)
- Special keys: `<Esc>`

`parseSequence` / `parseSequenceStrings` return
`std::expected<..., SequenceParseError>` with two error kinds:
`UnknownCharacter` (byte not recognized in command-mode slot) and
`MalformedSpecialKey` (`<` without a valid closing `>` in a
command-mode slot). Insert-mode typed text stays *tolerant*: a bare
`<` with no closing `>` is treated as a literal character, matching
Vim's own behavior for typed text.

The error channel is consumed only at the FFI boundary
(`vimficiency_tokenize_sequence`); internal callers receive
optimizer-produced sequences and call `.value()`. See the ADR
section "Error-handling boundary" for the rationale.

`formatSequenceForDisplay` is an exception to the assert-fast rule:
it's fed captured user keystrokes via Lua and falls back to the raw
string on parse failure.

**Usage**: FFI output and debug display. Raw sequences are used internally for execution and cost calculation.

## Debug (`Debug.h`)

`debug()` macro for conditional debug output. Enabled when `VIMFICIENCY_DEBUG=true` (default in debug builds).
