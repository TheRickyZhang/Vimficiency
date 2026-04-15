-- tests/lua/test_session_window.lua
-- Covers compute_result_for_active's window-fallback (#1): when the
-- original session window is closed but the buffer is still current via
-- a different window, we should proceed instead of hard-rejecting.

local session = require("vimficiency.session")
local util = require("vimficiency.util")

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
  local buf = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "hello world", "second line" })
  vim.api.nvim_set_current_buf(buf)

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

  -- Cleanup: close scratch buffer
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("compute_result_for_active: buffer-gone still hard-rejects", function()
  local buf = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "x" })
  vim.api.nvim_set_current_buf(buf)
  local active = fake_active(buf, vim.api.nvim_get_current_win())

  vim.cmd("enew")
  vim.api.nvim_buf_delete(buf, { force = true })

  local result, err = session.compute_result_for_active(active)
  assert_eq(result, nil, "should fail when buffer gone")
  assert_true(tostring(err or ""):find("buffer", 1, true),
    "error must mention buffer: " .. tostring(err))
end)

test("compute_result_for_active: different-buffer still hard-rejects", function()
  local buf_a = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf_a, 0, -1, false, { "a" })
  vim.api.nvim_set_current_buf(buf_a)
  local active = fake_active(buf_a, vim.api.nvim_get_current_win())

  local buf_b = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf_b, 0, -1, false, { "b" })
  vim.api.nvim_set_current_buf(buf_b)

  local result, err = session.compute_result_for_active(active)
  assert_eq(result, nil, "should fail when current buf differs")
  assert_true(tostring(err or ""):find("not in original buffer", 1, true),
    "error must identify buffer mismatch: " .. tostring(err))

  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)
