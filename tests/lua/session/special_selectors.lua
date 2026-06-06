-- tests/lua/session/special_selectors.lua
-- The `$` selector: most-recent suggest regression. Distinct from `@`
-- (most-recent finished, any kind) and resolved through the shared
-- `session_store.resolve_special` path.

local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

--- Seed a finished record under `alias`, finishing with `finish_alias`.
local function seed_finished(alias, finish_alias, result)
  local id = "test-special-" .. alias
  ---@diagnostic disable-next-line: missing-fields
  session_store.store_manual(alias, {
    id = id, key_nsid = -1, status = "active", key_seq = {},
  })
  session_store.finish_session(
    id, result or h.fake_result({ user_seq = "" }), finish_alias, nil, "manual")
  return id
end

test("$ resolves to the pinned suggest regression, independent of @", function()
  local suggest_id = seed_finished("regr", "regr", h.fake_result({ user_seq = "ciwx" }))
  session_store.set_last_suggest_id(suggest_id)

  -- A later, unrelated finish moves @ but must not touch $.
  seed_finished("other", "other")

  assert_eq(session.default_save_name("@"), "other")

  local dollar, err = session.resolve_result("$")
  assert_true(dollar ~= nil, "$ resolves to the pinned regression: " .. tostring(err))
  assert_eq(dollar.user_seq, "ciwx")
  assert_eq(session.default_save_name("$"), "regr")
end)

test("$ unset yields a regression-specific error", function()
  -- Simulate an evicted/never-set pin: the id has no backing record, so
  -- get_last_suggest_id (and thus resolve_special) report nothing.
  session_store.set_last_suggest_id("test-special-evicted")

  local res, err = session.resolve_result("$")
  assert_eq(res, nil)
  assert_true(err and err:find("regression") ~= nil,
    "error should mention regression: " .. tostring(err))
end)
