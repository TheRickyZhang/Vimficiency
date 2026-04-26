-- tests/lua/save_default.lua
-- Covers `session.default_save_name` — the helper behind
-- `:Vimfy save <selector>` when the user omits the filename.
--
-- Rule under test: any literal selector is returned verbatim (every
-- valid alias shape — `a`, `refactor`, `5`, `3s` — already passes
-- `is_valid_saved_name`). `@` resolves to the alias the caller passed
-- to `session_store.finish_session`, which is stored on the record at
-- finish time.

local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")

--- Seed the store with a manual session under `alias`, then finish it
--- with `finish_alias` so `@` has something to resolve to. Returns the
--- id so tests can tear down if they want to.
local function seed_and_finish_manual(alias, finish_alias)
  local id = "test-save-default-" .. alias
  ---@diagnostic disable-next-line: missing-fields
  session_store.store_manual(alias, {
    id = id,
    key_nsid = -1,   -- skip key_tracking.detach in finish_session
    status = "active",
    key_seq = {},
  })
  -- A dummy result table is fine; default_save_name never inspects it.
  session_store.finish_session(id, { user_seq = "" }, finish_alias, nil, "manual")
  return id
end

test("default_save_name: literal selectors are returned verbatim", function()
  for _, selector in ipairs({ "a", "refactor", "WIP", "5", "123", "3s", "30s" }) do
    assert_eq(session.default_save_name(selector), selector, selector)
  end
end)

test("default_save_name: @ resolves to the last finish alias", function()
  seed_and_finish_manual("savedefault", "savedefault")
  assert_eq(session.default_save_name("@"), "savedefault")
  -- Also verifies the reason stamping: the seed helper passes
  -- `"manual"` through finish_session, so the stored result must carry
  -- the reason — the header formatter depends on this field.
  local last_result = session_store.get_last_finished_result()
  assert_eq(last_result and last_result.finish_reason, "manual")

  -- Manual session, but finished under a recall-form alias — exercises
  -- the "caller's literal alias is what we store" path. The store
  -- doesn't care that the record is manual; `@` resolution is governed
  -- entirely by whatever the finish caller passed.
  seed_and_finish_manual("anothername", "3s")
  assert_eq(session.default_save_name("@"), "3s")

  seed_and_finish_manual("firstalias", "firstalias")
  seed_and_finish_manual("secondalias", "secondalias")
  assert_eq(session.default_save_name("@"), "secondalias")
end)
