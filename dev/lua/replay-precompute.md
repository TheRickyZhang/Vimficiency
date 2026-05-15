# Replay precompute

How the simulate/replay UI faithfully replays a user-captured key sequence
on top of the original buffer content, and how that precompute stays
correct under Neovim's async input loop.

Code of record: `lua/vimficiency/simulate.lua`.

---

## The goal

The user captures a sequence of motion keystrokes (`jf;i<BS>2<Esc><Space>ve`)
against a buffer slice. The replay UI needs to show, at every intermediate
step `k` in that sequence:

- the buffer contents after `k` tokens have been applied;
- the cursor position after `k` tokens;
- the mode after `k` tokens (so `i` colors the cursor "insert",
  `v`/`V`/`<C-v>` colors it "visual", etc.).

Two constraints drive the design:

1. **Scrubbing is `O(1)`.** The user can `<Left>`/`<Right>`/`[b`/`]b`
   arbitrarily; we cannot re-simulate from step 0 on every key. So every
   step's `{ lines, cursor, mode }` triple is precomputed up front and
   indexed by step.
2. **Mode-faithful.** `:normal` flattens insert/visual into a single
   atomic operation; that throws away the very mid-stream states the
   replay exists to surface. So we can't use `:normal` as the oracle.

## The oracle: a hidden probe window

The oracle is Neovim itself, driven through `vim.api.nvim_feedkeys`. For
each sequence we:

1. Create an off-screen 1×1 floating window (`probe_win`) over a scratch
   buffer seeded with the user's line snapshot.
2. Set the cursor to the captured start position.
3. Drive tokens through the probe one at a time, sampling
   `{ lines, cursor, mode }` after each token.
4. Tear down the probe (window + buffer) when done.

Key reasons for the 1×1 float rather than `nvim_buf_call`:

- `nvim_feedkeys` queues keys to the input queue to drain *later*; at
  drain time, whichever window is **actually current** receives them. If
  we used `nvim_buf_call` to run the feedkeys inside a callback that
  merely swaps current-buffer for its duration, keys queued inside that
  callback could drain into the user's real buffer after it returns —
  leaking input.
- The float must be focusable and really current, not just
  "temporarily" current. Making `probe_win` the live current window for
  the entire precompute avoids the race entirely.

`noautocmd = true` and `buftype = nofile` / `bufhidden = wipe` keep the
probe invisible to user autocmds and plugin machinery.

## Sequential, not concurrent

`M.simulate_compare` is called with N sequences (user seq + top-M
optimals). Each gets its own probe, one at a time, via
`precompute_next(idx)` which calls `precompute_states(...)` and only
starts the next sequence from its `on_done` callback. This is deliberate:

- Only one probe is ever current at a time — no cross-talk.
- A cancellation token (`precompute_gen`) invalidates stale callbacks if
  the user fires a new `simulate_compare` before the previous finishes.

## Tokenization (`tokenize_for_animation`)

The raw captured sequence is a single string. To render one token per
step we first tokenize it. Three tiers:

1. **C++ `tokenize_sequence`** — the canonical tokenizer the optimizer
   already uses. Understands counts, operators, text objects, named-key
   forms (`<BS>`, `<Esc>`, etc.), and motion combinators.
2. **C++ `tokenize_movements`** — fallback for sequences the sequence
   tokenizer rejects (e.g., when the buffer context doesn't match).
3. **Per-character Lua fallback** — last resort; keeps `<Key>` forms
   intact.

Two normalization rules matter for replay:

- **Keep feedable command units intact.** Commands that consume exactly
  one following keystroke (`f`, `F`, `t`, `T`, `r`, `m`, `'`, `` ` ``,
  `@`) must include that key in the token, so `f;` remains one token when
  semicolon is the find target. Char-find repeats stay separate replay
  steps (`fa;` → `fa`, `;`) so Neovim carries find state between tokens.
  The Lua character fallback applies the same feedable-unit rule when
  both C++ tokenizers reject the sequence.
- **Chunk plain insert-mode text into 4-char segments** for nicer animation
  (`"hello world"` → `["hell", "o wo", "rld"]`). `<...>` key notation is
  kept intact as its own step, because captured insert-mode spaces and
  returns arrive from `keytrans` as feedable forms like `<Space>` and `<CR>`.

Both rules are visible via `M._debug_tokenize_for_animation` and
exercised by `tests/lua/simulate/tokenize.lua`.

## Feeding tokens: the drain problem

Given a tokenized sequence, the naïve loop is:

```lua
for _, token in ipairs(tokens) do
  nvim_feedkeys(token, "n", false)     -- queue the key
  -- sample here
end
```

The problem: `nvim_feedkeys(..., "n", false)` **queues** `token` into the
typeahead buffer and returns immediately. Neovim's input loop drains
typeahead on some later tick. If we sample before the drain, we capture
stale state. If we sample after the drain, we capture correct state. So
we need to *wait for the drain*.

### Previous strategy: coroutine yields

The precompute originally ran inside a `coroutine.create(...)`, with a
helper:

```lua
local function feed_and_yield(keys)
  nvim_set_current_win(probe_win)
  nvim_feedkeys(keys, "n", false)
  coroutine.yield()       -- give Neovim one tick to drain typeahead
  coroutine.yield()       -- give it another tick to stabilize nvim_get_mode()
end
```

The driver (`step()`) resumes the coroutine, and when the coroutine
yields, schedules its next resume via `vim.defer_fn(step, 0)` — a libuv
0ms timer. Two yields = two libuv-timer-callback gaps.

Empirically, the original author found one yield was enough for the
typeahead drain in most cases, but `nvim_get_mode()` sometimes lagged
one tick behind a mode transition (notably `i` → `<Esc>`). Two yields
stabilized the mode query. Comment from the code:

> Empirically, a single defer tick is enough for the input queue to
> drain, but `nvim_get_mode()` sometimes lags one more tick behind a
> mode transition (observed reliably for `i` → `<Esc>` with only one
> intermediate feed). Two yields makes the mode query stable across
> all mode transitions we care about.

### Why the two-yield strategy was probabilistic, not deterministic

The assumption "1 libuv tick ≥ 1 typeahead drain" holds *most of the
time* but not *all of the time*. `vim.defer_fn(cb, 0)` schedules a
libuv timer; the libuv loop fires its timer-callback phase at specific
points. Neovim's input-loop pass (the thing that actually drains
typeahead and dispatches keys through `vgetc`) is a separate pass.
Whether an input-drain pass runs *between* our two timer callbacks is
not guaranteed. Sometimes they're interleaved (good); sometimes two
timer callbacks fire back-to-back with no input-drain pass in between
(bad, the race).

The bug surfaced as a **first-token dropout** under live load: running
the capture under a live Neovim with autocmds, UI, and other event-loop
competitors, the first `$` of a precompute would occasionally produce
a snapshot where the cursor hadn't moved. Ad-hoc probes around each
`feed_and_yield` (sampled at `before_feed` / `after_feedkeys` /
`after_yield_1` / `after_yield_2`) showed:

```
window[2] token "$":
  after_yield_2    cursor=(2,2) mode=n   ← NEVER drained

window[3] token "$" (same starting state, next sequence):
  after_yield_2    cursor=(2,7) mode=n   ← drained
```

Identical inputs, identical time budgets (~0.5ms between yields),
different outcomes. That rules out wall-clock-insufficiency and points
squarely at nvim's input-loop scheduling.

### The fix: drain ordinary tokens, replay edit transactions

Neovim's `nvim_feedkeys` mode string has an `x` flag documented as
"execute commands until typeahead is empty". That's a synchronous drain:
by the time feedkeys returns, the queued key has been processed by the
same code path `vgetc` uses. No more racing with the event loop.

We still cannot blanket-switch to `x`: replay needs visible Insert and
Visual snapshots, not only final Normal-mode results. The current
precompute therefore has two paths.

Ordinary tokens use synchronous drain whenever the probe is not in
Insert/Replace and the token itself does not enter a modal state:

```lua
local curr_mode = nvim_get_mode().mode
local insert_like = curr_mode:sub(1, 1) == "i" or curr_mode:sub(1, 1) == "R"
if not insert_like and not enters_modal_state(token) then
  nvim_feedkeys(keys, "nx", false)     -- synchronous drain
else
  nvim_feedkeys(keys, "n", false)      -- fall through to yield path
end
coroutine.yield()
coroutine.yield()
```

`enters_modal_state(token)` is a trivial view over parser metadata:
`token.kind == "change"` or `"visual"`. Those kinds are tagged by the
C++ `SequenceParser` and preserved across the FFI as
`<kind>\t<text>\n`, so Lua does not maintain a second command grammar.

Insert-producing edit transactions use a different path. A transaction
is:

```
change typed* escape?
```

For each visible step, precompute replays the corresponding prefix from
the pre-edit snapshot:

```
ciw
ciw2
ciw2<Space>
ciw2<Space>*
ciw2<Space>*<Space>
ciw2<Space>*<Space>i
ciw2<Space>*<Space>i<Esc>
```

This keeps one replay snapshot per token, but avoids feeding `ciw`,
then `2`, then `<Space>` as unrelated Normal-mode inputs. That matters
in headless Neovim: `nvim_feedkeys("ciw", "n", false)` can leave the
operator/change pending until the next typed key arrives, so the replay
would capture stale Normal-mode snapshots and eventually show only the
first typed character.

Plain insert entries (`i`, `A`, etc.) still use the yielding path for
the entry snapshot because Neovim can report their cursor/mode
faithfully. Their typed prefixes are then replayed from the same
pre-edit checkpoint. After a transaction-ending `<Esc>`, precompute
lets InsertLeave settle and reapplies the recorded final snapshot
before continuing with the next token; otherwise the hidden probe can
inherit Neovim's delayed cursor adjustment.

A minimal Lua classifier persists for one narrow case: the char-by-char
fallback path in `tokenize_for_animation`, taken when both C++
tokenizers fail on truly malformed input. `classify_fallback` there
covers the same insert/visual bare tokens as best-effort, defaulting
to `"motion"` (fast-path-eligible, safe). This residual coupling is
scoped to one block and documented in-line.

The two yields still run for every token, including the `nx` path, for
three reasons:

1. The existing mode-query lag concern (`i` → `<Esc>` stabilization) is
   untouched and still covered.
2. The yields are zero-cost when there's nothing left to drain —
   `vim.defer_fn(_, 0)` is a single event-loop tick.
3. Future tokens in the same sequence may enter modal state; keeping
   the yield scaffold in place means we don't have to re-introduce it.

In practice this means pure-Normal motions (`j`, `w`, `$`, `fa`, `;`, `de`)
are deterministically drained, which is exactly the class of tokens
the bug was dropping.

## Tail `<Esc>`

After the last user token, the coroutine feeds one final `<Esc>` to
flush any residual mode (the sequence may end mid-insert or mid-visual
if the user didn't exit explicitly). That keeps stray keystrokes from
leaking into the user's real buffer when the probe is torn down — the
next `feed_and_yield` is guaranteed to start from Normal mode.

This `<Esc>` goes through the same branch logic: `curr_mode != "n"` (we
just ended in insert/visual), so it takes the yield-based path.

## Replay layer vs. precompute

Once precompute finishes, the replay UI in `M.simulate_compare` /
`build_sim_ui` just indexes into the precomputed `states[seq_idx][step
+ 1]` arrays and blits snapshots onto display buffers via
`apply_state`. The UI is a pure index lookup over precomputed state
— stepping, cycling (`[b`/`]b`), and focus (`<CR>` / `:Vimfy focus N`
/ `:Vimfy escape`) all share this lookup.

Because the UI is decoupled from the oracle, a precompute bug
manifests as a **wrong snapshot stored**, not a wrong *rendering*. The
`D` dump's juxtaposition of `rendered cursor` vs. `snapshot[k]` cursor
makes this decomposition explicit, which is how the drain-race
diagnosis was nailed down (with temporary per-token probes added
during the investigation and removed after the fix landed).

## Tests that guard this

- `tests/lua/simulate/tokenize.lua` — tokenization invariants.
- `tests/lua/simulate/integration.lua`
  - `simulate replays insert and visual sequence with virtual header`
    — exercises the yield path on modal transitions (`i`, `<BS>`,
    typed chars, `<Esc>`, `<Space>`, `v`, `e`).
  - `simulate handles leading space and append-at-eol` — `A`+typed
    via yield path.
  - `simulate precompute drains first normal-mode token before
    snapshot` — repeats the `j` / `$` / `$` three-sequence case from
    the live bug dump 20 times. Regression guard for the drain fix.
  - `session simulate replays saved-session dot-repeat ciw sequence`
    — runs through `session.simulate(...)`, not just `simulate_compare`,
    and catches edit-transaction prefixes such as
    `x.ciw2<Space>*<Space>i<Esc>`.
- `tests/lua/capture/on_key_mapping_probe.lua` — characterizes what
  `vim.on_key` emits for different mapping shapes. Adjacent concern,
  not precompute per se, but informs whether a captured sequence can
  contain a bound LHS (which would confuse the oracle).

## When to revisit

If a new mode-entering command needs to be recognized, extend the C++
`SequenceParser`'s `tryParseChange` / `tryParseVisual` (whichever
applies). The Lua side picks it up automatically via the kind wire.
Only touch `FALLBACK_*_BARE` tables in `simulate.lua` if the new
command also needs to work through the char-by-char fallback (rare —
the fallback now only fires on sequences the grammar genuinely can't
parse).

If the first-token regression test goes intermittent again, the `nx`
path is no longer sufficient — likely because a future Neovim version
changes feedkeys-`x` semantics. Widen the fast-path guard or add a
third yield.

If modal snapshots start drifting (insert/visual sampling captures the
wrong mode or cursor), the culprit is the yield path, not `nx`. Either
add a third yield or introduce a bounded poll (`vim.wait(N, pred)`)
keyed on mode/cursor stability.
