-- Characterization test for `vim.on_key()` under a few mapping shapes.
-- See `dev/lua/neovim_on_key_issues.md` for the human-typing caveat.

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
  vim.api.nvim_feedkeys(
    vim.api.nvim_replace_termcodes(lhs, true, false, true),
    "mxt", false)
  vim.on_key(nil, nsid)
  return events
end

---@param events table[]
---@return string
local function format_events(events)
  local dump = {}
  for i, e in ipairs(events) do
    dump[i] = string.format("  %d: key=%q typed=%q", i, e.key, e.typed)
  end
  return table.concat(dump, "\n")
end

--- Characterize `on_key` output across binding-shape variants of `<Space>ve`.
test("on_key: Lua-callback mapping (vimfy.map shape) emits ONE resolution event", function()
  vim.keymap.set("n", "<Space>ve", function() end)
  local events = record_events_for(" ve")
  vim.keymap.del("n", "<Space>ve")
  io.stdout:write("Lua-callback trace:\n" .. format_events(events) .. "\n")

  assert_eq(#events, 1, "expected exactly one event, got " .. #events)
  assert_eq(events[1].typed, "<Space>ve", "typed should carry the full LHS")
  assert_true(events[1].typed ~= events[1].key,
    "typed ~= key, so the dedup rule drops this event")
end)

test("on_key: string-RHS mapping emits individual pre-resolution keys", function()
  vim.keymap.set("n", "<Space>ve", "<Nop>")
  local events = record_events_for(" ve")
  vim.keymap.del("n", "<Space>ve")
  io.stdout:write("string-RHS trace:\n" .. format_events(events) .. "\n")

  -- No exact-count assertion; the dumped trace is the deliverable.
end)

test("on_key: <Plug>-remapped mapping behavior", function()
  vim.keymap.set("n", "<Plug>TestProbe", function() end)
  vim.keymap.set("n", "<Space>ve", "<Plug>TestProbe", { remap = true })
  local events = record_events_for(" ve")
  vim.keymap.del("n", "<Space>ve")
  vim.keymap.del("n", "<Plug>TestProbe")
  io.stdout:write("<Plug>-remap trace:\n" .. format_events(events) .. "\n")
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

  vim.api.nvim_feedkeys(
    vim.api.nvim_replace_termcodes(" ve", true, false, true),
    "mxt", false)
  vim.on_key(nil, nsid)
  vim.keymap.del("n", "<Space>ve")

  io.stdout:write("wrap-suppressed trace (ignoring column shows state AT event time):\n")
  for i, e in ipairs(events) do
    io.stdout:write(string.format("  %d: key=%q typed=%q ignoring=%s\n",
      i, e.key, e.typed, tostring(e.ignoring)))
  end

  assert_true(callback_fired, "mapping callback must have fired")

  local any_ignoring = false
  for _, e in ipairs(events) do
    if e.ignoring then any_ignoring = true break end
  end

  io.stdout:write(string.format(
    "  → any event fired with ignoring=true? %s\n", tostring(any_ignoring)))
end)

test("on_key: unbound LHS records every key as user input", function()
  local events = record_events_for(" ve")
  io.stdout:write("unbound trace:\n" .. format_events(events) .. "\n")

  assert_true(#events >= 3,
    "expected at least 3 events (one per key), got " .. #events)
  for i, e in ipairs(events) do
    assert_eq(e.typed, e.key,
      string.format("event %d: typed == key expected for unbound input", i))
  end
end)
