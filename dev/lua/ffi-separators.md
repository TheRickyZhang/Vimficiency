# FFI payload framing: separators and length-prefixing

Every FFI call that passes structured data between C++ and Lua has to
pick a wire format. This doc is the single source of truth for the
framing conventions the project uses, and explains which one to reach
for when you add a new payload.

Code of record:
- C++ constants and decoders: `src/LuaExports/Shared.h` and
  `src/LuaExports/UtilityExports.cpp`.
- Lua mirrors: `lua/vimficiency/ffi.lua` (top of file).

## Why this matters

Captured sequence bytes can contain anything: literal spaces from
insert-mode text, tabs, newlines, `<Esc>` (0x1b), or control bytes from
remapped `<C-x>` keys. Any ad-hoc delimiter that looks "safe" at a
glance — space, pipe, comma — has already bitten us. The rule below
exists because we have been here more than once.

**Rule.** When a field *could* contain arbitrary bytes, use
length-prefixing. When fields *are guaranteed* to be printable with no
ASCII control bytes, use the shared control-character separators. Never
invent a per-site delimiter.

## Convention 1: Length-prefixed strings

Format: `N:<N bytes><next length>:<...>...` concatenated until EOF.

Best when: the field content is user-provided or otherwise unbounded
(buffer line arrays, raw captured event payloads, anything that could
carry UTF-8 or control bytes).

- Lua encoder: `encode_string_list` in `lua/vimficiency/ffi.lua`.
- C++ decoder: `payload::decodeLengthPrefixedStrings` in
  `src/LuaExports/UtilityExports.cpp`, with typed wrappers
  `decodeLineArray`, `decodeRecallRecordMeta`.

Pros: collision-free by construction. The length tells the decoder
exactly how many bytes to consume, so no content byte is ever
interpreted as a delimiter.

Cons: two passes to emit (length then body); slightly more verbose on
the wire.

## Convention 2: Control-character separators

Format: records separated by `\x1e` (ASCII Record Separator); fields
within a record separated by `\x1f` (ASCII Unit Separator).

- Shared constants:
  - C++: `vimficiency::lua_exports::kEventFieldSep` (0x1f),
    `kEventRecordSep` (0x1e), in `src/LuaExports/Shared.h`.
  - Lua: `EVENT_FIELD_SEP`, `EVENT_RECORD_SEP`, at the top of
    `lua/vimficiency/ffi.lua`.
- Example encoder: `M.build_sequence` in `ffi.lua` joins
  `{mode, 0x1f, keytrans(key), 0x1e}` per event.
- Example decoder: `payload::decodeKeyTrackingEvents` in
  `UtilityExports.cpp`.
- Helper: `export_helpers::packInts(a, b)` in `Shared.h` for the
  trivial two-field case.

Best when: every field is guaranteed to be "printable in a narrow
sense" — keytrans-formatted text (`<C-_>` rather than raw 0x1f),
mode strings (`"n"`, `"i"`, `"no"`, `"v"`), numeric strings, or
optimizer-synthesized sequences whose byte vocabulary is bounded.

Pros: cheap to emit, single linear scan to decode.

Cons: **it only works if the content can't contain 0x1e or 0x1f**.
This invariant is load-bearing. When it can be ensured statically
(e.g., output of `keytrans` is always printable), enforce it with an
explicit assert at the encoder boundary, as `M.build_sequence` does:

```lua
assert(not ev.key_typed:find("[\x1e\x1f]"),
  "build_sequence: key_typed contains a record/field separator; " ..
  "callers must pass keytrans'd strings")
```

The assert is not overflow paranoia — it is a contract statement that
protects the framing. An invariant violation would silently desync the
decoder on the C++ side, so surfacing it at the encoder is correct.

## Convention 3: Newline-separated text output

Format: one record per line, `\n`-terminated.

Used only for the analyze-results payload:
`src/LuaExports/AnalyzeExports.cpp` and its Lua consumer in
`ffi.lua` (`parse_analyze_results`).

Per-line structure:
```
size: <N> user_cost: <X.XXX>\n                            (header)
<raw_seq_bytes>\x1f<cost>\n                               (body, repeated)
[ ----------------DEBUG----------------  ... ]            (optional trailer)
```

The header is label-prefix parsed (`user_cost:%s*(%S+)`), which is
robust even against trailing whitespace. The body line uses
`kEventFieldSep` (0x1f) between the raw sequence bytes and the numeric
cost — **this used to be a literal space**, which silently corrupted
any sequence containing insert-mode text with a space (`ihello world`).
The space worked 95% of the time and broke in exactly the cases that
matter, which is the canonical "never use a delimiter that can appear
in your content" failure mode.

Why newlines still separate records here (instead of `\x1e`): the
payload needs a printable DEBUG trailer that is grepped for by a
literal substring match (`----------------DEBUG----------------`), and
keeping records newline-terminated makes that trailer naturally fit at
the end of the stream. The body-line framing inside each record
(`<seq>\x1f<cost>`) still follows Convention 2.

## Checklist when adding a new FFI payload

1. **Can a field contain arbitrary bytes?** If yes, use Convention 1
   (length-prefixed). Do not try to invent an "unlikely" delimiter.
2. **If all fields are guaranteed printable-narrow**, use Convention 2
   (`kEventFieldSep` / `kEventRecordSep`). **Assert the invariant at
   the encoder.** Do not rely on convention alone.
3. **Reuse the shared constants.** Never hardcode `'\x1f'` or `'\x1e'`
   at a call site — import the constant so a future format tweak is a
   one-line change.
4. **Mirror the Lua and C++ decoders as a pair in the same change.**
   A bug here is invisible until an edge case hits production; keep
   the two sides trivially cross-referenceable.
5. **Add a direct unit test for the parser** with synthetic input
   containing the worst-case characters you think couldn't appear,
   plus the ones that actually can (whitespace, control bytes, UTF-8
   multibyte). See `tests/lua/ffi_analyze.lua` for the pattern.

## Numeric field encoding across the FFI

Integer payloads carried as text fields (e.g. `time_started` in the
recall-record meta stream) have their own failure mode: Lua's
`tostring(n)` emits scientific notation for numbers large enough to
lose representational precision as decimals. For an hrtime-scale value
(~1.07e14 ns on a mid-uptime box), `tostring` produces
`"1.0710102841774e+14"`. C++'s `from_chars<int64_t>` does not accept
scientific notation, silently fails, and the payload decoder returns
an error — which, in the `storeIntOr(0, …)` wrapper pattern, turns
into a zero return that Lua maps to nil. The symptom is a "query
returns nothing" that looks like a logic bug but is actually an
encoding bug.

**Rule.** When an integer-valued Lua number crosses the FFI as a text
field, encode it with the `encode_int64` helper in `ffi.lua`, not
`tostring`. The helper uses `string.format("%.0f", n)` which always
emits decimal form without scientific notation.

```lua
-- WRONG: breaks silently for large hrtime values.
parts[#parts + 1] = tostring(rec.time_started)

-- RIGHT:
parts[#parts + 1] = encode_int64(rec.time_started)
```

This only applies to numbers serialized into text payloads. Direct
FFI arguments typed as `int64_t` in the cdef (e.g. the
`target_hrtime` parameter of `vimficiency_resolve_recall_cutoff`)
convert correctly via LuaJIT's automatic Lua-number → int64_t coercion
— no helper needed there.

### A better architecture (when it's worth it)

The `encode_int64` helper is a workaround for two underlying facts:
(1) we serialize numeric payloads as decimal text, and (2) Lua doubles
can't losslessly represent int64 values above 2^53 (~104 days of ns
uptime). The workaround is adequate because our comparisons are
relative — small drifts above 2^53 affect both the record and the
target identically. But if an int64-identity-preserving payload is
ever needed, the structural fix is:

1. Use LuaJIT's `ffi.new("int64_t", n)` cdata to carry the value, and
2. Change the wire format from length-prefixed text to a binary
   layout that passes int64s by their native 8-byte representation.

Both changes are invasive relative to the current codebase, so we
default to the `encode_int64` helper and flag this block for future
reference.

## Anti-patterns and why they keep appearing

- **Space-as-delimiter "because cost can't contain whitespace".** True
  for the cost, false for the sequence. This exact bug lived in
  `AnalyzeExports.cpp` until the 0x1f switchover. The temptation
  reappears every time someone reads the parse regex in isolation
  without tracing where the sequence bytes come from.
- **"Split on the last space".** A local fix that relies on an
  invariant about one specific field. Safer than "split on first
  space", still fragile: any future addition of a second post-seq
  field breaks it silently. We rejected this in favor of 0x1f.
- **Per-site ad-hoc delimiters (`|`, `~`, `::`).** Each new delimiter
  is a new invariant to remember and a new decoder-desync vector.
  Reuse the shared constants.
- **Silent content escaping** (`\\n` → `\n`, etc.). Works, but adds an
  escape-sequence grammar no one documents. Length-prefixing is
  strictly simpler.
- **`tostring(n)` on large Lua numbers before FFI encode.** See the
  numeric-field section above. `tostring` produces scientific notation
  at ~1e14 and above; `from_chars<int64_t>` rejects it; the payload
  silently fails to decode. Always use `encode_int64`.
