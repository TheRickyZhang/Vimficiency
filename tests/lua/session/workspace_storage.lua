-- tests/lua/workspace_storage.lua
-- Covers the workspace/storage split: save / store / fetch and the
-- implicit-fetch fallback in simulate(). These exercise the plumbing in
-- session.lua + session/store.lua that routes finished results between
-- in-memory aliases and on-disk files.

local h = require("_helpers")
local session = require("vimficiency.session")
local session_store = require("vimficiency.session.store")

--- Use a temp dir for the saved/ directory so tests don't touch the user's
--- real `stdpath("data")/vimficiency/saved`. `stdpath("data")` respects
--- `$XDG_DATA_HOME`, so overriding that for the test run is sufficient.
---@return string tmp_dir
local function use_temp_save_dir()
  local tmp = vim.fn.tempname()
  vim.fn.mkdir(tmp, "p")
  vim.env.XDG_DATA_HOME = tmp
  return tmp
end

--- Construct a minimal VF.Session.Result for register_fetched_result.
---@param user_seq string
---@return table
local function fake_result(user_seq)
  return {
    lines = { "hello" },
    start_row = 0, start_col = 0, end_row = 0, end_col = 0,
    user_seq = user_seq or "abc",
    user_cost = 1.0,
    optimal_results = { { seq = user_seq or "abc", cost = 1.0 } },
    start_time = 0, key_count = 0, timestamp = 0,
    finish_reason = "manual",
  }
end

test("workspace/storage: save copies to disk without touching memory", function()
  use_temp_save_dir()
  h.silence_notify(function()
    local id, err = session_store.register_fetched_result("aaa", fake_result("user-seq"))
    assert_true(id, "seed failed: " .. tostring(err))

    session.save("aaa", "aaa")

    -- Memory still has it (save is a copy, not a move).
    assert_true(session_store.has_result("aaa"),
      "save must not remove the in-memory alias")

    -- Disk has it.
    local saved_list = session.list_saved()
    local found = false
    for _, n in ipairs(saved_list) do if n == "aaa" then found = true end end
    assert_true(found, "disk copy missing after save")

    session_store.remove(session_store.get_id("aaa"))
  end)
end)

test("workspace/storage: store moves from memory to disk", function()
  use_temp_save_dir()
  h.silence_notify(function()
    local id = session_store.register_fetched_result("bbb", fake_result("user-seq"))
    assert_true(id, "seed failed")

    session.store("bbb", "bbb")

    -- Memory no longer has it.
    assert_true(not session_store.has_result("bbb"),
      "store must remove the in-memory alias")

    -- Disk has it.
    local saved_list = session.list_saved()
    local found = false
    for _, n in ipairs(saved_list) do if n == "bbb" then found = true end end
    assert_true(found, "disk copy missing after store")
  end)
end)

test("workspace/storage: fetch copies from disk to memory, disk preserved", function()
  use_temp_save_dir()
  h.silence_notify(function()
    -- Seed a session, save it, close the in-memory copy, then fetch back.
    local id = session_store.register_fetched_result("ccc", fake_result("yank"))
    assert_true(id, "seed failed")
    session.save("ccc", "ccc")
    session_store.remove(session_store.get_id("ccc"))
    assert_true(not session_store.has_result("ccc"), "precondition: memory cleared")

    session.fetch("ccc", "ccc")

    -- Memory populated from disk.
    assert_true(session_store.has_result("ccc"),
      "fetch must register an in-memory alias")
    local result = session_store.get_result("ccc")
    assert_eq(result.user_seq, "yank", "fetched result round-tripped")

    -- Disk copy untouched.
    local saved_list = session.list_saved()
    local found = false
    for _, n in ipairs(saved_list) do if n == "ccc" then found = true end end
    assert_true(found, "fetch must not delete the disk copy")

    session_store.remove(session_store.get_id("ccc"))
  end)
end)

test("workspace/storage: fetch refuses to overwrite an in-memory alias", function()
  use_temp_save_dir()
  h.silence_notify(function()
    -- Seed both memory and disk under the same name.
    local id = session_store.register_fetched_result("ddd", fake_result("mem"))
    assert_true(id, "seed failed")
    session.save("ddd", "ddd")

    -- Now try to fetch into the same alias. Must refuse, leaving memory
    -- untouched.
    session.fetch("ddd", "ddd")

    local result = session_store.get_result("ddd")
    assert_eq(result.user_seq, "mem",
      "fetch-into-occupied-alias must not overwrite memory")

    session_store.remove(session_store.get_id("ddd"))
  end)
end)

test("workspace/storage: save overwrite-warns but does not refuse", function()
  use_temp_save_dir()
  h.silence_notify(function()
    -- First save.
    local id1 = session_store.register_fetched_result("eee", fake_result("v1"))
    assert_true(id1, "seed failed")
    session.save("eee", "eee")

    -- Second save under same name with different content.
    local result = session_store.get_result("eee")
    result.user_seq = "v2"  -- mutate in-place
    session.save("eee", "eee")

    -- Disk has the second version.
    session_store.remove(session_store.get_id("eee"))
    session.fetch("eee", "eee")
    assert_eq(session_store.get_result("eee").user_seq, "v2",
      "overwrite should have replaced the disk copy")

    session_store.remove(session_store.get_id("eee"))
  end)
end)

test("workspace/storage: register_fetched_result rejects non-manual alias", function()
  h.silence_notify(function()
    local id, err = session_store.register_fetched_result("3s", fake_result())
    assert_true(not id, "alias '3s' must be rejected")
    assert_match(err or "", "not a valid manual alias")
  end)
end)
