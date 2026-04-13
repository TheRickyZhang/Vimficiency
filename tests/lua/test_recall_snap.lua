-- tests/lua/test_recall_snap.lua
-- Covers the backward-snap algorithm used by `:Vimfy end Ns`. Pure-function
-- tests over session_store._pure.snap_backward_to_boundary, so we can feed
-- synthetic ring state and assert precisely.

local store = require("vimficiency.session_store")
local snap = store._pure.snap_backward_to_boundary

-- Build a synthetic ring. Each entry is (id, first_mode).
---@param entries table<integer, { id: string, first_mode: string }>
---@return table records, string[] order
local function build(entries)
  local records, order = {}, {}
  for _, e in ipairs(entries) do
    records[e.id] = { first_mode = e.first_mode }
    order[#order + 1] = e.id
  end
  return records, order
end

test("snap: cutoff already on a clean boundary returns itself", function()
  local records, order = build({
    { id = "a", first_mode = "n" },
    { id = "b", first_mode = "n" },
    { id = "c", first_mode = "n" },
  })
  assert_eq(snap(records, order, 3, 20), 3)
end)

test("snap: walks back over insert-mode records to reach normal", function()
  local records, order = build({
    { id = "a", first_mode = "n" },
    { id = "b", first_mode = "i" },
    { id = "c", first_mode = "i" },
    { id = "d", first_mode = "i" },
  })
  assert_eq(snap(records, order, 4, 20), 1)
end)

test("snap: walks back over operator-pending (no) records", function()
  local records, order = build({
    { id = "a", first_mode = "n"  },
    { id = "b", first_mode = "no" },
    { id = "c", first_mode = "no" },
  })
  assert_eq(snap(records, order, 3, 20), 1)
end)

test("snap: exhausts budget without finding boundary, returns nil", function()
  local records, order = build({
    { id = "a", first_mode = "n" },
    { id = "b", first_mode = "i" },
    { id = "c", first_mode = "i" },
    { id = "d", first_mode = "i" },
    { id = "e", first_mode = "i" },
  })
  assert_eq(snap(records, order, 5, 2), nil)
end)

test("snap: budget of 0 requires cutoff itself to be a boundary", function()
  local records, order = build({
    { id = "a", first_mode = "n" },
    { id = "b", first_mode = "i" },
  })
  assert_eq(snap(records, order, 2, 0), nil)
  assert_eq(snap(records, order, 1, 0), 1)
end)

test("snap: record with no first_mode is not a boundary", function()
  local records, order = { a = {}, b = { first_mode = "n" } }, { "a", "b" }
  assert_eq(snap(records, order, 1, 20), nil)
  assert_eq(snap(records, order, 2, 20), 2)
end)

test("snap: visual mode (v) is not a clean boundary", function()
  local records, order = build({
    { id = "a", first_mode = "n" },
    { id = "b", first_mode = "v" },
  })
  assert_eq(snap(records, order, 2, 20), 1)
end)
