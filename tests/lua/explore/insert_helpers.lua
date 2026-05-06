local insert_repair = require("vimficiency.explore.insert_repair")

local function chunk_text(plan)
  local out = {}
  for _, chunk in ipairs(plan.chunks) do
    out[#out + 1] = chunk.kind .. ":" .. chunk.text
  end
  return out
end

test("explore insert repair returns literal suffix for matching prefix", function()
  local plan = insert_repair.plan("2 * i", "2 ")
  assert_eq(plan.backspace_count, 0)
  assert_eq(plan.literal_suffix, "* i")
  assert_eq(chunk_text(plan), { "literal:* i" })
end)

test("explore insert repair backspaces to the correct prefix on mismatch", function()
  local plan = insert_repair.plan("2 * i", "2x")
  assert_eq(plan.backspace_count, 1)
  assert_eq(plan.correct_prefix, "2")
  assert_eq(plan.literal_suffix, " * i")
  assert_eq(chunk_text(plan), { "key:<BS>", "literal: * i" })
end)

test("explore insert repair can finish with only backspace", function()
  local plan = insert_repair.plan("2", "2x")
  assert_eq(plan.backspace_count, 1)
  assert_eq(plan.literal_suffix, "")
  assert_eq(chunk_text(plan), { "key:<BS>" })
end)

test("explore insert repair treats less-than as literal buffer text", function()
  local plan = insert_repair.plan("< x", "<")
  assert_eq(plan.backspace_count, 0)
  assert_eq(plan.literal_suffix, " x")
  assert_eq(chunk_text(plan), { "literal: x" })
end)
