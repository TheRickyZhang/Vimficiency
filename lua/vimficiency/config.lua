-- lua/vimficiency/config.lua
-- Unified place to modify configuration, which is used by lua and C/C++ parsing

local M = {
  RESULTS_CALCULATED = 20, -- Should be >= RESULTS_SAVED, at most 20.
  RESULTS_SAVED = 5,       -- should be in [1, 8], otherwise too much overhead
  SLICE_PADDING = 5,       -- For our specific sections, how many extra lines should we consider? (TODO: check whether this for both motions and composition?)
  SLICE_EXPAND_TO_PARAGRAPH = false, -- Do we also want to include the current paragraph? (If we use {} or p a lot)
  MAX_SEARCH_LINES = 500,    -- maximum number of contiguous lines to search

  -- Recall ring: a session is retained if EITHER cap still holds it
  -- (union semantics). Evicted only when both caps say drop.
  KEY_SESSION_CAPACITY = 200,   -- count floor for retention
  MAX_RETENTION_SECONDS = 120,  -- age floor for retention

  -- In-progress manual sessions are dropped when EITHER this idle timeout
  -- elapses since the last captured key, OR the cursor drifts more than
  -- MAX_SEARCH_LINES rows from the session's start row (the optimizer
  -- rejects such a session at `end` time anyway). Not user-configurable:
  -- the policy is a safety net for forgotten `:Vimfy start`, not a knob.
  MANUAL_IDLE_TIMEOUT_SECONDS = 300,

  -- Backward snap budget: when resolving `end Ns`, how many sessions back
  -- we'll walk looking for a clean normal-mode command boundary before
  -- giving up.
  SNAP_LOOKBACK_KEYS = 20,

  -- Auto-suggest (Suggest type — auto start, auto end): populated by
  -- init.lua's validator from the user's `auto_suggest` setup key.
  -- `false` means disabled. When enabled, the value is a table of parsed
  -- triggers + cooldown_ms. See docs/user/06-suggest.md for shape.
  auto_suggest = false,

  -- Watch type — manual start, auto end. Populated from the `watch` setup
  -- key. `false` means disabled. When enabled, the value is a table
  -- { idle_ms, cooldown_ms }. Independent from `auto_suggest`; both can
  -- run at once with different thresholds. See docs/user/04-watch.md.
  watch = false,
}

return M
