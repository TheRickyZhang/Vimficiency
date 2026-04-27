-- Smoke tests for the `:Vimfy reload` Lua path.

local vimfy = require("vimficiency")

test("reload: setup is idempotent (two calls in a row don't crash)", function()
  vimfy.setup({})
  vimfy.setup({})
  assert_true(true, "survived two setups")
end)

test("reload: shutdown detaches global on_key subscribers", function()
  vimfy.setup({})
  local kt = require("vimficiency.capture.key_tracking")
  assert_true(kt.is_global_attached("recall_capture"),
    "recall_capture subscriber present after setup")
  vimfy.shutdown()
  assert_true(not kt.is_global_attached("recall_capture"),
    "recall_capture subscriber gone after shutdown")
end)

test("reload: reload_lua swaps module identities and preserves finished records", function()
  vimfy.setup({})

  local store = require("vimficiency.session.store")
  local id = "reload-test-id"
  local stash = store.dump_for_reload()
  stash.records[id] = {
    id = id,
    status = "finished",
    result = {
      lines = { "hello" },
      start_row = 0, start_col = 0,
      end_row = 0, end_col = 0,
      user_seq = "",
      optimal_results = {},
    },
  }
  stash.last_finished = id
  store.restore_from_dump(stash)
  assert_true(store.get_last_finished_result() ~= nil,
    "seeded finished record is visible before reload")

  local pre_store = store

  local stats = vimfy.reload_lua(nil)
  assert_true(stats.preserved_records >= 1,
    "reload reports >=1 preserved record; got " .. tostring(stats.preserved_records))

  local post_store = require("vimficiency.session.store")
  assert_true(pre_store ~= post_store,
    "session.store module table identity changed across reload")

  assert_true(post_store.get_last_finished_result() ~= nil,
    "finished record survived reload")
end)

test("reload: FFI load path respects __vimficiency_reload_lib_path if set", function()
  -- A bogus path should fall through to the normal load path.
  _G.__vimficiency_reload_lib_path = "/nonexistent/path/libvimficiency.so"
  package.loaded["vimficiency.ffi"] = nil
  local ok, ffi_mod = pcall(require, "vimficiency.ffi")
  _G.__vimficiency_reload_lib_path = nil
  assert_true(ok,
    "ffi module still loads when the preferred path is invalid; fell through correctly: "
    .. tostring(ffi_mod))
end)
