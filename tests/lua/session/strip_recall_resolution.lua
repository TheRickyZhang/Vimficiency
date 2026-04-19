-- tests/lua/session/strip_recall_resolution.lua
-- Direct unit test for `session_store.strip_recall_pre_resolution`, the hook
-- the global on_key listener calls when it sees a multi-key mapping
-- resolution event. Path B in dev/lua/neovim_on_key_issues.md: pre-
-- resolution single-byte events already flowed through `ingest_recall_event`
-- into every active recall record's key_seq, and must be retroactively
-- removed so Recall sessions don't record the mapping's LHS as motion.

local session_store = require("vimficiency.session.store")
local h = require("_helpers")

---@param key_typed_raw string   Raw single-byte (or multi-byte) key.
---@return table   minimal VimficiencyKeyEvent
local function ev(key_typed_raw)
  return {
    t = 0, mode = "n", win = 0, buf = 0,
    key_sent_raw = key_typed_raw,
    key_sent = key_typed_raw,
    key_typed_raw = key_typed_raw,
    key_typed = key_typed_raw,
  }
end

test("strip_recall_pre_resolution: pops matching tail from every active record", function()
  local _, buf_a, id_a = h.seed_recall({ lines = { "a" } })
  local _, buf_b, id_b = h.seed_recall({ lines = { "b" } })

  -- Seed each active record's key_seq with prior motion + the three
  -- pre-resolution pending bytes of <Space>ve (" ve"). key_count follows.
  local rec_a = session_store._for_test_get_active(id_a)
  local rec_b = session_store._for_test_get_active(id_b)
  for _, e in ipairs({ ev("j"), ev("w"), ev(" "), ev("v"), ev("e") }) do
    table.insert(rec_a.key_seq, e); rec_a.key_count = (rec_a.key_count or 0) + 1
    table.insert(rec_b.key_seq, e); rec_b.key_count = (rec_b.key_count or 0) + 1
  end

  session_store.strip_recall_pre_resolution(" ve")

  assert_eq(#rec_a.key_seq, 2, "record A keeps prior motion only")
  assert_eq(rec_a.key_seq[1].key_typed_raw, "j", "record A first event preserved")
  assert_eq(rec_a.key_seq[2].key_typed_raw, "w", "record A second event preserved")
  assert_eq(rec_a.key_count, 2, "record A key_count adjusted down by 3")

  assert_eq(#rec_b.key_seq, 2, "record B keeps prior motion only")
  assert_eq(rec_b.key_count, 2, "record B key_count adjusted down by 3")

  session_store.remove(id_a)
  session_store.remove(id_b)
  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)

test("strip_recall_pre_resolution: no-op when tail doesn't match", function()
  local _, buf, id = h.seed_recall({ lines = { "a" } })
  local rec = session_store._for_test_get_active(id)
  for _, e in ipairs({ ev("j"), ev("w") }) do
    table.insert(rec.key_seq, e); rec.key_count = (rec.key_count or 0) + 1
  end

  -- Resolution typed = " ve" but tail is "jw" — no match, nothing popped.
  session_store.strip_recall_pre_resolution(" ve")

  assert_eq(#rec.key_seq, 2, "key_seq unchanged when tail mismatches")
  assert_eq(rec.key_count, 2, "key_count unchanged when tail mismatches")

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("strip_recall_pre_resolution: skips finished records (key_seq is nil)", function()
  local _, buf, id = h.seed_recall({ lines = { "a" } })
  local rec = session_store._for_test_get_active(id)
  rec.status = "finished"
  rec.key_seq = nil

  -- No crash. Silent skip.
  local ok = pcall(session_store.strip_recall_pre_resolution, " ve")
  assert_true(ok, "must not crash on finished records")

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)
