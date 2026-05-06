-- tests/lua/session_window.lua
-- Covers compute_result_for_active's window-fallback (#1): when the
-- original session window is closed but the buffer is still current via
-- a different window, we should proceed instead of hard-rejecting.

local session = require("vimficiency.session")
local ffi_lib = require("vimficiency.ffi")
local util    = require("vimficiency.util")
local h       = require("_helpers")

local function fake_active(buf, win)
  return {
    id = "test-" .. tostring(vim.uv.hrtime()),
    key_nsid = -1,
    win = win,
    buf = buf,
    start_state = util.capture_state(buf, win),
    time_started = vim.uv.hrtime(),
    status = "active",
    key_seq = {},
    key_count = 0,
    first_mode = "n",
    result = nil,
  }
end

test("compute_result_for_active: window-closed fallback to current window", function()
  local buf = h.new_buf({ "hello world", "second line" })

  -- Two windows, both showing buf. We'll treat the current one as
  -- "original", then close it and land in the split.
  local win_original = vim.api.nvim_get_current_win()
  vim.cmd("vsplit")
  local win_other = vim.api.nvim_get_current_win()
  assert_true(win_original ~= win_other, "sanity: vsplit must yield a new window")

  local active = fake_active(buf, win_original)

  -- Focus the split, close the original window. We're now in win_other
  -- but still in `buf`.
  vim.api.nvim_set_current_win(win_other)
  vim.api.nvim_win_close(win_original, true)
  assert_eq(vim.api.nvim_get_current_buf(), buf,
    "sanity: still in original buffer after closing original window")
  assert_true(not vim.api.nvim_win_is_valid(win_original),
    "sanity: original window is gone")

  local result, err = session.compute_result_for_active(active)
  -- The optimizer may still reject for unrelated reasons (empty keyseq,
  -- trivial slice). The assertion is narrower: the failure must NOT be
  -- window-related.
  if result == nil then
    local msg = tostring(err or "")
    assert_true(not msg:find("window", 1, true),
      "failure must not be window-related, got: " .. msg)
    assert_true(not msg:find("original window", 1, true),
      "failure must not mention original window, got: " .. msg)
  end

  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("compute_result_for_active: buffer-gone still hard-rejects", function()
  local buf = h.new_buf({ "x" })
  local active = fake_active(buf, vim.api.nvim_get_current_win())

  vim.cmd("enew")
  vim.api.nvim_buf_delete(buf, { force = true })

  local result, err = session.compute_result_for_active(active)
  assert_eq(result, nil, "should fail when buffer gone")
  assert_match(err, "buffer", "error must mention buffer")
end)

test("compute_result_for_active: different-buffer still hard-rejects", function()
  local buf_a = h.new_buf({ "a" })
  local active = fake_active(buf_a, vim.api.nvim_get_current_win())
  local buf_b = h.new_buf({ "b" })

  local result, err = session.compute_result_for_active(active)
  assert_eq(result, nil, "should fail when current buf differs")
  assert_match(err, "buf " .. buf_a, "error must name captured buffer")
  assert_match(err, "buf " .. buf_b, "error must name current buffer")

  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)

test("compute_result_for_active: includes capture key debug", function()
  local buf = h.new_buf({ "cout << i+1 << endl;" })
  local win = vim.api.nvim_get_current_win()
  vim.api.nvim_win_set_cursor(win, { 1, 8 })
  local active = fake_active(buf, win)
  active.key_seq = {
    { t = 1000, mode = "n",  key_sent = "c", key_sent_raw = "c", key_typed = "c", key_typed_raw = "c", win = win, buf = buf },
    { t = 2000, mode = "no", key_sent = "W", key_sent_raw = "W", key_typed = "W", key_typed_raw = "W", win = win, buf = buf },
    { t = 3000, mode = "i",  key_sent = "W", key_sent_raw = "W", key_typed = "W", key_typed_raw = "W", win = win, buf = buf },
    { t = 4000, mode = "i",  key_sent = "2", key_sent_raw = "2", key_typed = "2", key_typed_raw = "2", win = win, buf = buf },
  }
  active.key_count = #active.key_seq
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "cout << W2 << endl;" })

  local result
  h.with_patch({
    { ffi_lib, "analyze", function()
      return {}, 9.0, ""
    end },
  }, function()
    result = assert(session.compute_result_for_active(active))
  end)

  assert_eq(result.user_seq, "cWW2")
  assert_eq(result.capture_debug.raw_joined, "cWW2")
  assert_eq(result.capture_debug.reduced_sequence, "cWW2")
  assert_eq(result.capture_debug.normalized_sequence, "cWW2")
  assert_eq(result.capture_debug.event_count, 4)
  assert_eq(result.capture_debug.events[2].mode, "no")
  assert_eq(result.capture_debug.events[3].key_typed, "W")

  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)
