-- tests/lua/recall_snap.lua
-- Covers the backward-snap algorithm used by `:Vimfy recall Ns`. Pure-function
-- tests over session_store._pure.snap_backward_to_boundary, so we can feed
-- synthetic ring state and assert precisely.

local store = require("vimficiency.session.store")
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

test("snap: returns clean normal-mode boundary", function()
  local records, order = build({
    { id = "a", first_mode = "n" },
    { id = "b", first_mode = "n" },
    { id = "c", first_mode = "n" },
  })
  assert_eq(snap(records, order, 3, 20), 3)

  records, order = build({
    { id = "a", first_mode = "n" },
    { id = "b", first_mode = "i" },
    { id = "c", first_mode = "i" },
    { id = "d", first_mode = "i" },
  })
  assert_eq(snap(records, order, 4, 20), 1)
end)

test("snap: skips non-normal starting modes", function()
  local records, order = build({
    { id = "a", first_mode = "n"  },
    { id = "b", first_mode = "no" },
    { id = "c", first_mode = "no" },
  })
  assert_eq(snap(records, order, 3, 20), 1)

  records, order = build({
    { id = "a", first_mode = "n" },
    { id = "b", first_mode = "v" },
  })
  assert_eq(snap(records, order, 2, 20), 1)
end)

test("snap: returns nil when budget cannot reach a boundary", function()
  local records, order = build({
    { id = "a", first_mode = "n" },
    { id = "b", first_mode = "i" },
    { id = "c", first_mode = "i" },
    { id = "d", first_mode = "i" },
    { id = "e", first_mode = "i" },
  })
  assert_eq(snap(records, order, 5, 2), nil)

  records, order = build({
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
