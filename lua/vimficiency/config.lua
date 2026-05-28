-- lua/vimficiency/config.lua
-- Central config surface. Add or change accepted top-level config keys here.

local fields = {
  -- Optimizer/search knobs
  RESULTS_CALCULATED = 20,
  RESULTS_SAVED = 5,
  SLICE_PADDING = 5,
  SLICE_EXPAND_TO_PARAGRAPH = false,
  MAX_SEARCH_LINES = 500,

  -- User overrides for shared base optimizer parameters. Empty by
  -- default — C++ owns the canonical defaults (`OptimizerParamsBase.h`).
  -- Anything the user sets here flows to every canonical optimizer call
  -- (marks/watches/recall/auto/explore-defaults) as `shared:` overrides.
  -- Explore-session sidecar overrides (set live via the explore panel)
  -- shadow these per-session.
  --
  -- Recognized keys: maxResults, maxNodesPopped, exploreFactor,
  -- effortWeight, distanceWeight, minPrefixCount, maxPrefixCount. See
  -- `OptimizerParamsBase.h` for semantics and defaults.
  optimizer = {},

  -- Recall ring / session safety
  KEY_SESSION_CAPACITY = 200,
  MAX_RETENTION_SECONDS = 120,
  MANUAL_IDLE_TIMEOUT_SECONDS = 300,
  SNAP_LOOKBACK_KEYS = 20,

  -- Session features
  auto_suggest = {
    cooldown_ms = 5000,
  },
  watch = false,

  sequence_display = {
    tokenize = true,   -- true: add spaces between parsed tokens (`ciw foo <Esc>`); false: keep raw adjacency (`ciwfoo<Esc>`)
    sectionize = true, -- true: split movement/edit phases onto separate display lines; false: keep one flat display line
  },

  -- Explore-view UI preferences. Schema + built-in defaults live in
  -- `lua/vimficiency/explore/settings.lua` as `VIEW_SETTINGS` (the runtime
  -- source of truth). This table is just the user-override bag: values placed
  -- here via `setup({ explore = { ... } })` shadow the built-in defaults.
  -- Runtime toggles via the `gs` modal also persist here in-memory and
  -- reset on restart — declare overrides in `setup()` to make them
  -- permanent.
  explore = {},

  play = {
    include_user_sequence = true,                  -- show the user's typed sequence as the leftmost pane
    window_count = 2,                              -- total panes shown side-by-side; 1..4 (user pane consumes one slot if included)
  },

  -- C++-owned config surface
  default_keyboard = require("vimficiency.config_detail").cpp(),
  weights = require("vimficiency.config_detail").cpp(),
  keys = require("vimficiency.config_detail").cpp(),
  slice_buffer_amount = require("vimficiency.config_detail").cpp(),
  shiftwidth = require("vimficiency.config_detail").cpp(),
  use_count_penalty_overrides = require("vimficiency.config_detail").cpp(),
  count_penalty_overrides = require("vimficiency.config_detail").cpp(),
}

function fields.is_lua_key(name)
  return fields._defaults[name] ~= nil
end

function fields.reset()
  for name, value in pairs(fields._defaults) do
    fields[name] = vim.deepcopy(value)
  end
end

function fields.apply(user_config)
  local detail = require("vimficiency.config_detail")

  user_config = user_config or {}
  fields.reset()

  local merged = vim.tbl_deep_extend("force", vim.deepcopy(fields._defaults), user_config)
  merged.watch = detail.normalize_watch(user_config.watch)
  merged.auto_suggest = detail.normalize_auto_suggest(user_config.auto_suggest, fields._defaults.auto_suggest)

  for name, value in pairs(merged) do
    if fields._defaults[name] ~= nil then
      fields[name] = value
    end
  end

  local consumed = {}
  for name in pairs(user_config) do
    if fields.is_lua_key(name) then
      consumed[name] = true
    end
  end
  return consumed
end

fields._defaults = require("vimficiency.config_detail").capture_defaults(fields)
fields._for_test = fields._for_test or {}
fields._for_test.fields = fields

fields.reset()

return fields
