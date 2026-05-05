# ffi-boundary

Use when adding or modifying any code that crosses the Lua/C++ FFI boundary,
any file under `src/lua/` or the Lua plugin tree, or any C++ symbol exported
for FFI. Also use when deciding whether a new piece of logic belongs in Lua
or in C++.

The Lua side of Vimficiency is intentionally thin. It exists to do the things
only Lua can do; nothing else.

## The split — non-negotiable

Lua is allowed to own ONLY:

- Calls into Neovim's native runtime API (`vim.api.*`, `vim.fn.*`,
  `vim.notify`, autocmds, keymaps, `on_key`, etc.).
- Local persistent storage that requires Neovim's filesystem / `stdpath`
  conventions (session storage, on-disk caches scoped to the editor).
- The minimal mechanics needed to organize the above to and from C++ over FFI.

Everything else lives in C++.

If you find yourself reaching for nontrivial logic in Lua, stop. Either:
1. Move it to C++ and expose a single FFI entry point, or
2. Justify it explicitly

## Naming — same identifier, snake_case, every side

**The rule:** the same identifier appears in C++, in `Api.def`, and in
`ffi.lua`, in snake_case. No case conversion, no translation.

The only adjustments are namespace prefixes that exist because C has no
namespaces:

- **Functions** are prefixed `vf_` in `Api.def` (always, on every export);
  the Lua wrapper strips it. `vf_explore_destroy` ↔ `M.explore_destroy`.
- **Struct types** are prefixed `VF` in `Api.def` (`VFScoreWeights`) and
  `VF.C.` in Lua-LS annotations (`VF.C.ScoreWeights`); the stem matches
  the C++ name (`ScoreWeights`).
- **Fields** have no prefix — they're already namespaced by their struct.

C++ structs whose layout *is* the wire format (`ScoreWeights`, `KeyInfo`,
the `VFConfig` payload) follow the boundary's snake_case so the FFI shim
is a plain member-by-member copy. Other C++ code keeps its normal
camelCase — it doesn't cross the boundary, so the rule doesn't apply.


## Encoding rules

- **int64 over FFI:** `tostring(n)` on hrtime-scale numbers emits scientific
  notation (`1.7e+18`), which `from_chars<int64_t>` rejects. Use the
  `encode_int64` helper in `ffi.lua` for any int64 crossing the boundary.
  See `dev/lua/` and `memory/ffi_int64_encoding.md`.
- Strings cross as `const char*` + length where possible; do not assume
  null-termination on the C++ side for buffer content sourced from Neovim.

## No dead fallbacks

- Do **not** wrap FFI calls in `pcall` to "be safe." If the C++ side errored,
  surface the real error — silent fallbacks hide bugs that only show up
  later as wrong answers.
- Do not branch on optional fields that the API contract guarantees are
  present. Trust the contract; fix it if it is wrong.
- This applies in both directions: C++ should not silently accept malformed
  input from Lua either.

## React to Neovim, do not predict it

The Lua side observes Neovim state via events and queries — it does not
parse the user's keystrokes to anticipate what Neovim will do.
