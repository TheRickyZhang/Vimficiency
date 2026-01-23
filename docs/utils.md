# Utility Patterns


General-purpose utilities in `src/Utils/`.

## Bitmask Flag Structs

### QuoteFlags / BracketFlags (`QuoteFlags.h`)

Lightweight bitmask structs for tracking which delimiter types have been seen during line scanning (e.g., for quote/bracket text objects).

**Design**: Direct mask constants avoid runtime comparisons when the delimiter type is already known from parsing context.

```cpp
struct QuoteFlags {
  uint8_t flags = 0;

  // Direct masks - use when type is known (0 comparisons)
  static constexpr uint8_t DoubleQuote = 1;  // "
  static constexpr uint8_t SingleQuote = 2;  // '
  static constexpr uint8_t Backtick = 4;     // `

  void add(uint8_t mask) { flags |= mask; }
  bool seen(uint8_t mask) const { return flags & mask; }

  // When you have raw char (2 comparisons)
  static constexpr uint8_t maskFor(char c);
};

struct BracketFlags {
  // Same pattern
  static constexpr uint8_t Paren = 1;   // ()
  static constexpr uint8_t Square = 2;  // []
  static constexpr uint8_t Curly = 4;   // {}

  // maskFor accepts either opener or closer (4 comparisons)
  static constexpr uint8_t maskFor(char c);
};
```

**Usage patterns**:
```cpp
// When parsing already identifies the type (optimal)
if (c == '"') {
  flags.add(QuoteFlags::DoubleQuote);  // 0 comparisons
}

// When type is unknown
flags.add(QuoteFlags::maskFor(c));     // 2 comparisons
```

**Why separate structs?** Quotes and brackets may have different functionality in the future (e.g., bracket matching requires pairing logic, quotes don't).

## Lines (`Lines.h`)

Wrapper around `vector<string>` with position-aware character access. Key methods:
- `get(pos)` - returns char at position, `'\n'` for empty lines
- `getNextPos()` / `getPrevPos()` - step to adjacent char (skip empty lines)
- `getNextPosIncludeEmpty()` / `getPrevPosIncludeEmpty()` - step including empty lines

See `boundary-logic.md` for when to use each variant.

## StringUtils (`StringUtils.h`)

String manipulation helpers, including `flattenLines()` for buffer-to-string conversion used in edit distance analysis.

## Debug (`Debug.h`)

`debug()` macro for conditional debug output. Enabled when `VIMFICIENCY_DEBUG=true` (default in debug builds).
