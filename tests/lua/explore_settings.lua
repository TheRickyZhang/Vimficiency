-- Tests for explore's settings layer: v1 → v2 sidecar migration,
-- default-shape construction, and resolve_param's per-scope → shared
-- → nil fallback.
local explore = require("vimficiency.explore")
local settings_profile = require("vimficiency.settings_profile")

local helpers = explore._for_test

test("explore settings: migrate_v1_to_v2 maps known flat keys into sections", function()
  local v1 = {
    display_mode = "above",
    recommendation_count = 7,
    show_user_typed = false,
    show_result_count = 3,
    nav_max_results_per_end_pos = 2147483647,
    transform_max_results_per_start_pos = 1,
  }
  local v2 = helpers.migrate_v1_to_v2(v1)

  assert_eq(v2.ui.display_mode, "above")
  assert_eq(v2.ui.recommendation_count, 7)
  assert_eq(v2.ui.show_user_typed, false)
  assert_eq(v2.ui.show_result_count, 3)
  assert_eq(v2.nav.maxResultsPerEndPos, 2147483647)
  assert_eq(v2.transform.maxResultsPerStartPos, 1)

  -- Untouched optimizer scopes are empty (C++ defaults apply).
  assert_eq(v2.shared, {})
  assert_eq(v2.composition, {})
end)

test("explore settings: migrate_v1_to_v2 drops unknown flat keys", function()
  local v1 = {
    display_mode = "below",
    -- Stale / typo'd keys that aren't part of the v1 surface — the
    -- migrator must drop them silently rather than parking them in
    -- some bucket.
    obsolete_setting = 99,
    nav_max_resullts_per_end_pos = 5,  -- typo
  }
  local v2 = helpers.migrate_v1_to_v2(v1)

  assert_eq(v2.ui.display_mode, "below")
  assert_eq(v2.shared, {})
  assert_eq(v2.nav, {})
  -- v2 has no top-level slot for unknown keys; if the migrator was
  -- routing junk somewhere we'd see it here as a non-nil scope field.
end)

test("explore settings: default_settings seeds UI from config.explore", function()
  local config = require("vimficiency.config")
  local defaults = helpers.default_settings()

  assert_eq(defaults.ui.display_mode, config.explore.display_mode)
  assert_eq(defaults.ui.recommendation_count, config.explore.recommendation_count)
  -- Optimizer scopes start empty — C++ owns those defaults.
  assert_eq(defaults.shared, {})
  assert_eq(defaults.nav, {})
  assert_eq(defaults.transform, {})
  assert_eq(defaults.composition, {})
end)

test("explore settings: settings_profile.load returns envelope with version", function()
  -- Round-trip a v2 envelope through the on-disk format.
  local path = vim.fn.stdpath("data") .. "/vimficiency/explore_settings.json"
  vim.fn.mkdir(vim.fn.fnamemodify(path, ":h"), "p")

  -- Hand-write a v1-shaped envelope so the loader has to detect the
  -- old version.
  local v1_envelope = vim.json.encode({
    version = 1,
    data = { display_mode = "highlight", nav_max_results_per_end_pos = 3 },
  })
  local fh = assert(io.open(path, "w"))
  fh:write(v1_envelope)
  fh:close()

  local envelope = settings_profile.load("explore")
  assert_eq(envelope.version, 1)
  assert_eq(envelope.data.display_mode, "highlight")
  assert_eq(envelope.data.nav_max_results_per_end_pos, 3)

  -- Cleanup so other tests don't see this fixture.
  os.remove(path)
end)

test("explore settings: settings_profile.load handles missing sidecar", function()
  -- The XDG_DATA_HOME the runner sets up may have a sidecar from a
  -- prior test; remove it so this exercises the cold path.
  local path = vim.fn.stdpath("data") .. "/vimficiency/explore_settings.json"
  os.remove(path)

  local envelope = settings_profile.load("explore")
  assert_eq(envelope.version, nil)
  assert_eq(envelope.data, {})
end)

test("explore settings: settings_profile.load handles malformed JSON", function()
  local path = vim.fn.stdpath("data") .. "/vimficiency/explore_settings.json"
  vim.fn.mkdir(vim.fn.fnamemodify(path, ":h"), "p")
  local fh = assert(io.open(path, "w"))
  fh:write("{not json")
  fh:close()

  local envelope = settings_profile.load("explore")
  assert_eq(envelope.version, nil)
  assert_eq(envelope.data, {})

  os.remove(path)
end)

test("explore settings: schema version is stable across reloads", function()
  -- The integer is part of the persistence contract; bumping it requires
  -- a migration. Pin it to catch accidental edits during refactor.
  assert_eq(helpers.EXPLORE_SCHEMA_VERSION, 2)
end)

test("explore settings: get_optimizer_defaults parses C++ defaults across types", function()
  -- The C++ side dumps every default-initialized param-struct field as
  -- `<scope>:<key>:<type>=<value>` lines. The Lua parser must produce
  -- typed values (numbers for int/double, booleans for bool). If a new
  -- field is added on either side the wire format keeps roundtripping;
  -- this test pins the per-type decoding so a regression there is loud.
  local ffi_lib = require("vimficiency.ffi")
  local defaults = ffi_lib.get_optimizer_defaults()

  -- maxResults is uniform across all three optimizers (base default
  -- 20). The historical Transform-only override was removed; if it
  -- comes back, surface it per-optimizer in the panel instead of
  -- continuing to fudge "shared" display.
  assert_eq(defaults.nav.maxResults, 20)
  assert_eq(defaults.transform.maxResults, 20)
  assert_eq(defaults.composition.maxResults, 20)

  -- Doubles parse as numbers, not strings.
  assert_eq(type(defaults.nav.exploreFactor), "number")
  assert_eq(defaults.nav.effortWeight, 1.0)
  assert_eq(defaults.composition.overshootPenalty, 3.0)

  -- Bools — the wire encodes true as "1" and false as "0".
  assert_eq(defaults.nav.useDirectionalPruning, true)
  assert_eq(defaults.composition.useDirectionalPruning, true)
end)
