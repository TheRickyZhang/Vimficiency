-- Characterization test for `vim.on_key()` under a few mapping shapes.
-- See `dev/lua/neovim_on_key_issues.md` for the human-typing caveat.

local h = require("_helpers")

-- Trace prints are documentation of *what Neovim did*, not assertion
-- evidence. On a passing run they're pure noise; on a failing run
-- they're diagnostic. Gate behind `VF_TEST_VERBOSE=1` to match the
-- runner's discipline — set it when debugging, leave it off otherwise.
local verbose = vim.env.VF_TEST_VERBOSE == "1"
local function trace(...)
  if verbose then io.stdout:write(...) end
end

---@param lhs string   keytrans-form LHS (e.g. " ve" for <Space>ve)
---@return table[]     ordered { key, typed } event log
local function record_events_for(lhs)
  local events = {}
  local nsid = vim.on_key(function(key, typed)
    events[#events + 1] = {
      key   = vim.fn.keytrans(key or ""),
      typed = vim.fn.keytrans(typed or ""),
    }
  end)
  -- Use the remap-aware helper because these tests deliberately exercise
  -- mapping resolution (the LHS may have a binding installed).
  h.feed_with_remap(lhs)
  vim.on_key(nil, nsid)
  return events
end

---@param lhs string
---@param lines string[]
---@param cursor [integer, integer]? 1-indexed row, 0-indexed col
---@return table[]
local function record_buffer_events_for(lhs, lines, cursor)
  local events = {}
  h.new_buf(lines)
  vim.api.nvim_win_set_cursor(0, cursor or { 1, 0 })
  local nsid = vim.on_key(function(key, typed)
    events[#events + 1] = {
      mode = vim.api.nvim_get_mode().mode,
      key = vim.fn.keytrans(key or ""),
      typed = vim.fn.keytrans(typed or ""),
    }
  end)
  h.feed(lhs)
  vim.on_key(nil, nsid)
  return events
end

---@param events table[]
---@return string
local function format_events(events)
  local dump = {}
  for i, e in ipairs(events) do
    dump[i] = string.format("  %d: mode=%q key=%q typed=%q",
      i, e.mode or "", e.key, e.typed)
  end
  return table.concat(dump, "\n")
end

--- Characterize `on_key` output across binding-shape variants of `<Space>ve`.
test("on_key: Lua-callback mapping (vimfy.map shape) emits ONE resolution event", function()
  vim.keymap.set("n", "<Space>ve", function() end)
  local events = record_events_for(" ve")
  vim.keymap.del("n", "<Space>ve")
  trace("Lua-callback trace:\n" .. format_events(events) .. "\n")

  assert_eq(#events, 1, "expected exactly one event, got " .. #events)
  assert_eq(events[1].typed, "<Space>ve", "typed should carry the full LHS")
  assert_true(events[1].typed ~= events[1].key,
    "typed ~= key, so the dedup rule drops this event")
end)

test("on_key: string-RHS mapping emits individual pre-resolution keys", function()
  vim.keymap.set("n", "<Space>ve", "<Nop>")
  local events = record_events_for(" ve")
  vim.keymap.del("n", "<Space>ve")
  trace("string-RHS trace:\n" .. format_events(events) .. "\n")

  -- No exact-count assertion; the dumped trace is the deliverable.
end)

test("on_key: <Plug>-remapped mapping behavior", function()
  vim.keymap.set("n", "<Plug>TestProbe", function() end)
  vim.keymap.set("n", "<Space>ve", "<Plug>TestProbe", { remap = true })
  local events = record_events_for(" ve")
  vim.keymap.del("n", "<Space>ve")
  vim.keymap.del("n", "<Plug>TestProbe")
  trace("<Plug>-remap trace:\n" .. format_events(events) .. "\n")
end)

test("on_key: vimfy.map-style wrap actually suppresses the event when it fires", function()
  -- Record whether any event fires during the wrapped callback's ignore window.
  local ignoring = false
  local events = {}
  local nsid = vim.on_key(function(key, typed)
    events[#events + 1] = {
      key        = vim.fn.keytrans(key or ""),
      typed      = vim.fn.keytrans(typed or ""),
      ignoring   = ignoring,
    }
  end)

  local callback_fired = false
  vim.keymap.set("n", "<Space>ve", function()
    ignoring = true
    callback_fired = true
    -- Simulate wrapped-callback body — just a short sync op.
    ignoring = false
  end)

  h.feed_with_remap(" ve")
  vim.on_key(nil, nsid)
  vim.keymap.del("n", "<Space>ve")

  trace("wrap-suppressed trace (ignoring column shows state AT event time):\n")
  for i, e in ipairs(events) do
    trace(string.format("  %d: key=%q typed=%q ignoring=%s\n",
      i, e.key, e.typed, tostring(e.ignoring)))
  end

  assert_true(callback_fired, "mapping callback must have fired")

  local any_ignoring = false
  for _, e in ipairs(events) do
    if e.ignoring then any_ignoring = true break end
  end

  trace(string.format(
    "  → any event fired with ignoring=true? %s\n", tostring(any_ignoring)))
end)

test("on_key: unbound LHS records every key as user input", function()
  local events = record_events_for(" ve")
  trace("unbound trace:\n" .. format_events(events) .. "\n")

  assert_true(#events >= 3,
    "expected at least 3 events (one per key), got " .. #events)
  for i, e in ipairs(events) do
    assert_eq(e.typed, e.key,
      string.format("event %d: typed == key expected for unbound input", i))
  end
end)

test("on_key: cW change motion trace", function()
  local events = record_buffer_events_for(
    "cW2<Space>*<Space>i<Esc>",
    { "cout << i+1 << endl;" },
    { 1, 8 })
  trace("cW trace:\n" .. format_events(events) .. "\n")

  assert_true(#events > 0, "expected key events for cW sequence")
end)
