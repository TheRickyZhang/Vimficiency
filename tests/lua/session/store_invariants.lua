-- Store-level invariants that do not belong to a single feature.

local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

test("get_active('Ns'): returns nil when ring is younger than N seconds", function()
  local _, buf, id = h.seed_recall()

  assert_eq(session_store.get_active("3s"), nil,
    "`3s` against a queue with a single ~0s-old record must not resolve")

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("get_active('1s'): resolves when a record is older than the window", function()
  local _, buf, id = h.seed_recall({ backdate_s = 5 })

  local resolved = session_store.get_active("1s")
  assert_true(resolved ~= nil, "`1s` must resolve when a 5s-old record exists")
  assert_eq(resolved.id, id)

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("remove: rejects alias-shaped strings", function()
  assert_error(function() session_store.remove("a") end,
    "not an alias", "remove must reject alias 'a'")
  assert_error(function() session_store.remove("3") end)
  assert_error(function() session_store.remove("3s") end)
end)

test("remove: accepts a well-formed session id (no-op when unknown)", function()
  local ok = pcall(session_store.remove, "NoName__20250101-000000-000__1__0001__b1")
  assert_eq(ok, true, "well-formed id must pass the guard")
end)

test("finish failure on a recall record preserves the ring slice", function()
  local _, buf_a, id = h.seed_recall({ backdate_s = 5 })

  local buf_b = h.new_buf({ "y" })

  local ok, err
  h.silence_notify(function() ok, err = pcall(session.finish, "3s") end)
  assert_true(ok, "finish should not propagate errors from a failed compute: " .. tostring(err))

  assert_true(session_store.summarize(id) ~= nil,
    "recall record must survive a finish() failure")

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)

test("can_store_manual: finished records do not consume capacity slots", function()
  h.new_buf()
  for _, alias in ipairs({ "capa", "capb", "capc", "capd", "cape" }) do
    session.start(alias)
    local rec = session_store.get_active(alias)
    assert_true(rec ~= nil, "start failed for " .. alias)
    session_store.finish_session(rec.id, { user_seq = "" }, alias, nil, "manual")
  end
  assert_eq(session_store.can_store_manual("capf"), true,
    "a new alias must be allowed when all existing records are finished")
end)

test("finish failure on a manual record removes it (regression guard)", function()
  local buf_a = h.new_buf()
  session.start("kfinishfail")
  assert_true(session_store.get_active("kfinishfail") ~= nil,
    "manual session should exist after start")

  local buf_b = h.new_buf({ "y" })
  h.silence_notify(function() pcall(session.finish, "kfinishfail") end)

  assert_eq(session_store.get_active("kfinishfail"), nil,
    "manual record must be removed after finish failure")

  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)
