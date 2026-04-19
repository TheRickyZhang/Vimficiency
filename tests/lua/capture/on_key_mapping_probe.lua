-- tests/lua/capture/on_key_mapping_probe.lua
-- Characterization test: record what `vim.on_key` actually emits when a user
-- types the LHS of a multi-key normal-mode mapping across four binding
-- shapes (Lua callback, string RHS, <Plug> remap, unbound). This pins down
-- the ground truth the capture pipeline is built on; if nvim's semantics
-- shift across versions, this test will tell us immediately.
--
-- Observed (nvim 0.11.x, 2026-04):
--   Lua callback  (vimfy.map shape):  1 event   — typed = full LHS, key != typed  → dropped
--   string RHS    (`"<Nop>"`, etc.):  1 event   — typed = full LHS, key != typed  → dropped
--   <Plug> remap:                      1 event   — typed = full LHS, key != typed  → dropped
--   unbound:                          N events   — one per key, typed == key       → RECORDED
--
-- IMPORTANT LIMITATION: this test drives input via `nvim_feedkeys`,
-- which queues all LHS bytes at once and lets nvim resolve the mapping
-- in a single input pass. At human typing speed, keys arrive one at a
-- time, nvim enters a pending-mapping state between them, and on_key
-- fires for each pending key *in addition to* the resolution event.
-- That extra pending-event behavior is NOT exercised here; see
-- `dev/lua/neovim_on_key_issues.md` § "Binding-shape characterization"
-- for the path-B writeup and the retroactive-strip fix it motivated.

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

--- Characterize on_key output across binding-shape variants of <Space>ve.
--- The concern the test addresses: the captured sequence in a real replay
--- included <Space>ve even though the user believes it's a Vimfy binding
--- and therefore should be suppressed. Each sub-test below pins down
--- behavior for one binding shape so we know exactly which shapes leak.
test("on_key: Lua-callback mapping (vimfy.map shape) emits ONE resolution event", function()
  vim.keymap.set("n", "<Space>ve", function() end)
  local events = record_events_for(" ve")
  vim.keymap.del("n", "<Space>ve")
  io.stdout:write("Lua-callback trace:\n" .. format_events(events) .. "\n")

  -- Expectation: a single event with typed = "<Space>ve" and key = the Lua
  -- callback sentinel (non-printable). `#typed > 1 and typed ~= key` drops
  -- it. No residual keys enter the capture stream.
  assert_eq(#events, 1, "expected exactly one event, got " .. #events)
  assert_eq(events[1].typed, "<Space>ve", "typed should carry the full LHS")
  assert_true(events[1].typed ~= events[1].key,
    "typed ~= key, so the dedup rule drops this event")
end)

test("on_key: string-RHS mapping emits individual pre-resolution keys", function()
  -- The shape doc-src/08-keymaps.md warns against: `nnoremap <LHS> :Cmd<CR>`.
  -- This is the shape most likely to leak into a captured motion stream,
  -- because nvim has nothing to suppress: there's no Lua wrapper to set
  -- the `ignoring` flag around the RHS.
  vim.keymap.set("n", "<Space>ve", "<Nop>")
  local events = record_events_for(" ve")
  vim.keymap.del("n", "<Space>ve")
  io.stdout:write("string-RHS trace:\n" .. format_events(events) .. "\n")

  -- No assertion on the exact count — this is a characterization test; the
  -- dumped trace is the deliverable. A later assertion can tighten once we
  -- decide what the fix should be.
end)

test("on_key: <Plug>-remapped mapping behavior", function()
  -- Shape from doc-src/08-keymaps.md section 2: bind user LHS to <Plug>
  -- name via `remap = true`. The <Plug> map itself is Lua-backed (set by
  -- `vimficiency.init.register_plug`), so the wrapped callback runs with
  -- `ignoring = true`.
  vim.keymap.set("n", "<Plug>TestProbe", function() end)
  vim.keymap.set("n", "<Space>ve", "<Plug>TestProbe", { remap = true })
  local events = record_events_for(" ve")
  vim.keymap.del("n", "<Space>ve")
  vim.keymap.del("n", "<Plug>TestProbe")
  io.stdout:write("<Plug>-remap trace:\n" .. format_events(events) .. "\n")
end)

test("on_key: vimfy.map-style wrap actually suppresses the event when it fires", function()
  -- The key question the `<Space>ve` investigation hinges on:
  -- does the single on_key event for a Lua-callback mapping fire *inside*
  -- the wrapped-callback's begin_ignore/end_ignore window, or outside it?
  -- If outside, the `ignoring` flag can't help us and the event leaks
  -- through despite the dedup rule (the dedup rule drops by
  -- `#typed > 1 and typed ~= key`, which is independent of ignoring — but
  -- the per-session `M.attach` `on_key` has a *separate* ignoring check
  -- that drops early, so ordering matters for what winds up in key_seq).
  --
  -- We simulate a vimfy.wrap-style callback by mutating a
  -- "ignoring-during-callback" flag, then record both the on_key events
  -- AND whether `ignoring` was set at the moment of each event.
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

  -- The binding-shape test above already established that one event
  -- fires with typed == full LHS. What this test pins down is WHEN it
  -- fires relative to the wrapped callback. Whichever is true for the
  -- current nvim, we want it recorded so the <Space>ve investigation
  -- has a deterministic answer.
  local any_ignoring = false
  for _, e in ipairs(events) do
    if e.ignoring then any_ignoring = true break end
  end

  -- Don't fail either way — this is characterization. The printed trace
  -- *is* the deliverable; future regressions in the ordering will be
  -- obvious in CI logs.
  io.stdout:write(string.format(
    "  → any event fired with ignoring=true? %s\n", tostring(any_ignoring)))
end)

test("on_key: unbound LHS records every key as user input", function()
  -- Baseline: no mapping at all. User literally typing <Space>, v, e with
  -- no binding. Expect three separate on_key events, each with typed == key.
  local events = record_events_for(" ve")
  io.stdout:write("unbound trace:\n" .. format_events(events) .. "\n")

  assert_true(#events >= 3,
    "expected at least 3 events (one per key), got " .. #events)
  for i, e in ipairs(events) do
    assert_eq(e.typed, e.key,
      string.format("event %d: typed == key expected for unbound input", i))
  end
end)
