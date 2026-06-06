-- tests/lua/workspace_storage.lua
-- Covers the workspace/storage split: save / store / fetch and the
-- implicit-fetch fallback in simulate(). These exercise the plumbing in
-- session.lua + session/store.lua that routes finished results between
-- in-memory aliases and on-disk files.

local h = require("_helpers")
local session = require("vimficiency.session")
local session_store = require("vimficiency.session.store")

local function remove_alias(alias)
  session_store.remove(assert(session_store.get_id(alias)))
end

local function fake_result(user_seq)
  return h.finished_result(user_seq)
end

test("workspace/storage: save copies to disk without touching memory", function()
  h.use_temp_data_home()
  h.silence_notify(function()
    local id, err = session_store.register_fetched_result("aaa", fake_result("user-seq"))
    assert_true(id, "seed failed: " .. tostring(err))

    session.save("aaa", "aaa")

    -- Memory still has it (save is a copy, not a move).
    assert_true(session_store.has_result("aaa"),
      "save must not remove the in-memory alias")

    -- Disk has it.
    assert_true(h.list_contains(session.list_saved(), "aaa"),
      "disk copy missing after save")

    remove_alias("aaa")
  end)
end)

test("workspace/storage: store moves from memory to disk", function()
  h.use_temp_data_home()
  h.silence_notify(function()
    local id = session_store.register_fetched_result("bbb", fake_result("user-seq"))
    assert_true(id, "seed failed")

    session.store("bbb", "bbb")

    -- Memory no longer has it.
    assert_true(not session_store.has_result("bbb"),
      "store must remove the in-memory alias")

    -- Disk has it.
    assert_true(h.list_contains(session.list_saved(), "bbb"),
      "disk copy missing after store")
  end)
end)

test("workspace/storage: fetch copies from disk to memory, disk preserved", function()
  h.use_temp_data_home()
  h.silence_notify(function()
    -- Seed a session, save it, close the in-memory copy, then fetch back.
    local id = session_store.register_fetched_result("ccc", fake_result("yank"))
    assert_true(id, "seed failed")
    session.save("ccc", "ccc")
    remove_alias("ccc")
    assert_true(not session_store.has_result("ccc"), "precondition: memory cleared")

    session.fetch("ccc", "ccc")

    -- Memory populated from disk.
    assert_true(session_store.has_result("ccc"),
      "fetch must register an in-memory alias")
    local result = session_store.get_result("ccc")
    result = assert(result)
    assert_eq(result.user_seq, "yank", "fetched result round-tripped")

    -- Disk copy untouched.
    assert_true(h.list_contains(session.list_saved(), "ccc"),
      "fetch must not delete the disk copy")

    remove_alias("ccc")
  end)
end)

test("workspace/storage: diffs survive a disk round-trip", function()
  h.use_temp_data_home()
  h.silence_notify(function()
    local result = fake_result("seq")
    result.prefix = ""
    result.suffix = ""
    result.diffs = {
      { init = { begin_row = 0, begin_col = 1, end_row = 0, end_col = 4 },
        goal = { begin_row = 0, begin_col = 1, end_row = 0, end_col = 5 } },
    }
    local id = session_store.register_fetched_result("diffroundtrip", result)
    assert_true(id, "seed failed")
    session.save("diffroundtrip", "diffroundtrip")
    remove_alias("diffroundtrip")

    session.fetch("diffroundtrip", "diffroundtrip")
    local loaded = assert(session_store.get_result("diffroundtrip"))
    assert_eq(#loaded.diffs, 1, "diffs array survived JSON round-trip")
    assert_eq(loaded.diffs[1].init.end_col, 4, "init span preserved")
    assert_eq(loaded.diffs[1].goal.end_col, 5, "goal span preserved")
    assert_eq(loaded.prefix, "", "prefix preserved")

    remove_alias("diffroundtrip")
  end)
end)

test("workspace/storage: fetch refuses to overwrite an in-memory alias", function()
  h.use_temp_data_home()
  h.silence_notify(function()
    -- Seed both memory and disk under the same name.
    local id = session_store.register_fetched_result("ddd", fake_result("mem"))
    assert_true(id, "seed failed")
    session.save("ddd", "ddd")

    -- Now try to fetch into the same alias. Must refuse, leaving memory
    -- untouched.
    session.fetch("ddd", "ddd")

    local result = session_store.get_result("ddd")
    result = assert(result)
    assert_eq(result.user_seq, "mem",
      "fetch-into-occupied-alias must not overwrite memory")

    remove_alias("ddd")
  end)
end)

test("workspace/storage: save overwrite-warns but does not refuse", function()
  h.use_temp_data_home()
  h.silence_notify(function()
    -- First save.
    local id1 = session_store.register_fetched_result("eee", fake_result("v1"))
    assert_true(id1, "seed failed")
    session.save("eee", "eee")

    -- Second save under same name with different content.
    local result = session_store.get_result("eee")
    result = assert(result)
    result.user_seq = "v2"  -- mutate in-place
    session.save("eee", "eee")

    -- Disk has the second version.
    remove_alias("eee")
    session.fetch("eee", "eee")
    local fetched = session_store.get_result("eee")
    fetched = assert(fetched)
    assert_eq(fetched.user_seq, "v2",
      "overwrite should have replaced the disk copy")

    remove_alias("eee")
  end)
end)

test("workspace/storage: save preserves capture_debug extras", function()
  local tmp = h.use_temp_data_home()
  h.silence_notify(function()
    local seeded = fake_result("debug-seq")
    seeded.capture_debug = {
      version = 1,
      event_count = 1,
      raw_joined = "debug-seq",
      reduced_sequence = "debug-seq",
      normalized_sequence = "debug-seq",
      events = {
        { index = 1, mode = "n", key_typed = "d" },
      },
    }
    local id = session_store.register_fetched_result("dbg", seeded)
    assert_true(id, "seed failed")

    session.save("dbg", "dbg")

    local path = tmp .. "/nvim/vimficiency/saved/dbg.json"
    local decoded = vim.json.decode(table.concat(vim.fn.readfile(path), "\n"))
    assert_eq(decoded.capture_debug.raw_joined, "debug-seq")
    assert_eq(decoded.capture_debug.events[1].mode, "n")

    remove_alias("dbg")
  end)
end)

test("workspace/storage: register_fetched_result rejects non-manual alias", function()
  h.silence_notify(function()
    local id, err = session_store.register_fetched_result("3s", fake_result("abc"))
    assert_true(not id, "alias '3s' must be rejected")
    assert_match(err or "", "not a valid manual alias")
  end)
end)
