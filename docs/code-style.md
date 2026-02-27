# Code Style Guide

Baseline: **Google C++ Style Guide**, with deviations noted below.

## Where We Differ from Google

| Construct | Google | Ours | Example | Reason |
|-----------|--------|------|---------|--------|
| Functions/methods | `PascalCase` | **`camelCase`** | `getLines()`, `parseSequence()` | Overwhelmingly used outside of Google conventions
| Constants | `kPascalCase` | **`UPPER_SNAKE_CASE`** | `POSITION_OUTSIDE_BOUNDARY`, `MAX_PREFIX_COUNT` | We sparingly use macros (all prefixed with VIMF), so low collision risk |
| Enum values | `kPascalCase` | **`PascalCase`** | `Mode::Normal`, `EdgeType::WordEdge` | `k` prefix is redundant with scoped enums (`enum class`) providing the namespace |
| Namespaces | `snake_case` | **`PascalCase`** | `VimCore`, `EditCategory` | We don't need visual disambiguation; combining snake_case and camelCase function names looks bad.
| File names | `snake_case.cc` | **`PascalCase.cpp`** | `EditOptimizer.cpp`, `CursorPos.h` | .cpp/.h is overwhelmingly used, and we will only transition to snake_case when developing windows support.

## Common Styles following Google

- **Classes/structs/template parameters**: `PascalCase` — `MotionState`, `CursorPos`, `template<typename T>`
- **Macros**: `UPPER_SNAKE_CASE`
- **Private members**: trailing underscore — `cost_`, `seq_`, `linesHash_`
- **Public struct fields**: plain `camelCase` — `line`, `col`, `targetCol`

### Constants

`UPPER_SNAKE_CASE` for all `constexpr` and compile-time constants:

```cpp
constexpr CursorPos POSITION_OUTSIDE_BOUNDARY{-1, -1, -1};
constexpr int TARGETCOL_EOL = INT_MAX;
inline constexpr bool DEBUG_ENABLED = true;
```

### Formatting

- **2-space indentation**, K&R / attached braces
- **`#pragma once`** instead of include guards
- **West `const`**: `const int x`, not `int const x`
- **Pointer/ref bind left**: `const Lines& lines`, not `const Lines &lines`
- **Early returns** encouraged
- **`using namespace`** allowed in `.cpp` files

### Types & Casting

- **`enum class`** (scoped enums) over unscoped `enum`
- **`using`** over `typedef`
- **`static_cast<>`** only — no C-style casts, no `reinterpret_cast` in normal code
- **Value semantics** preferred; smart pointers sparingly

### Language Features

- **C++23** (`CMAKE_CXX_STANDARD 23`)
- **No exceptions** — `assert()` for invariants, sentinel values for expected failures over `std::optional`
- **`auto`**: Use for complex types, iterators, lambdas. Spell out simple types (`int`, `bool`, `std::string`).

### Struct vs Class

- **`struct`**: Data-oriented types, even if they have methods. Public by default.
  `Range`, `CursorPos`, `KeyInfo`, `EditSearchContext`, `CountPenaltyParams`
- **`class`**: Types with meaningful encapsulation or invariants, where access control matters.
  `BracketFlags`, `EditState`, `ParsedEdit`
- Both may have `private:` sections; the distinction is about intent, not strict rules.

### Headers & Linkage

- Include order: `#pragma once` → standard library → blank line → project includes
- Anonymous `namespace {}` in `.cpp` for file-local helpers (not `static`)

### Comments

- `//` line comments only (no `/* */` blocks, no Doxygen)
- Section headers use `// ===...===` banners
- Comments explain *why*, not *what*

### Tests

- Framework: Google Test + Google Benchmark
- Test fixtures: `PascalCase` inheriting `::testing::Test`
- Test cases: `TEST_F(FixtureName, PascalCase_Description)`
- Tests live in `tests/` mirroring `src/` directory structure

