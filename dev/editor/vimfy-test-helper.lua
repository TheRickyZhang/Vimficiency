local M = {}

local macro_patterns = {
  { pattern = "^%s*FUZZ_TEST_F%s*%(%s*([%w_:]+)%s*,%s*([%w_]+)%s*%)" },
  { pattern = "^%s*FUZZ_TEST%s*%(%s*([%w_:]+)%s*,%s*([%w_]+)%s*%)" },
  { pattern = "^%s*TYPED_TEST_P%s*%(%s*([%w_:]+)%s*,%s*([%w_]+)%s*%)", wildcard = true },
  { pattern = "^%s*TYPED_TEST%s*%(%s*([%w_:]+)%s*,%s*([%w_]+)%s*%)", wildcard = true },
  { pattern = "^%s*TEST_P%s*%(%s*([%w_:]+)%s*,%s*([%w_]+)%s*%)", wildcard = true },
  { pattern = "^%s*TEST_F%s*%(%s*([%w_:]+)%s*,%s*([%w_]+)%s*%)" },
  { pattern = "^%s*TEST%s*%(%s*([%w_:]+)%s*,%s*([%w_]+)%s*%)" },
}

local targets = {
  unit = "vimfy_unit_tests",
  approval = "vimfy_approval_tests",
  property = "vimfy_property_tests",
  safety = "vimfy_safety_tests",
  debug = "vimfy_debug",
}

local fast_build_prereqs = {
  "vimfy_core",
  "test_utils",
}

local function shellescape(value)
  return vim.fn.shellescape(value)
end

local function cmake_build_cmd(build_dir, target)
  return "cmake --build " .. shellescape(build_dir) .. " --target " .. shellescape(target)
end

local function parse_macro(line)
  for _, macro in ipairs(macro_patterns) do
    local suite, name = line:match(macro.pattern)
    if suite then
      return suite, name, macro.wildcard or false
    end
  end
  return nil, nil, false
end

local function filter_for(suite, name, wildcard)
  if wildcard then
    return "*" .. suite .. "*." .. name .. "*"
  end
  return suite .. "." .. name
end

function M.repo_root()
  local source = debug.getinfo(1, "S").source
  if source:sub(1, 1) == "@" then
    return vim.fn.fnamemodify(source:sub(2), ":p:h:h:h")
  end
  return vim.fn.getcwd()
end

function M.test_kind_for_file(path)
  local file = (path or vim.fn.expand("%:p")):gsub("\\", "/")
  if file:find("/tests/Property/", 1, true) then
    return "property"
  end
  if file:find("/tests/Safety/", 1, true) then
    return "safety"
  end
  if file:find("/tests/Approval/", 1, true) then
    return "approval"
  end
  if file:find("/tests/Debug/", 1, true) then
    return "debug"
  end
  return "unit"
end

function M.find_test_at_cursor()
  local cursor_line = vim.api.nvim_win_get_cursor(0)[1]
  local lines = vim.api.nvim_buf_get_lines(0, 0, -1, false)

  local suite, name, wildcard = parse_macro(lines[cursor_line] or "")
  if suite then
    return { suite = suite, name = name, filter = filter_for(suite, name, wildcard) }
  end

  local word = vim.fn.expand("<cword>")
  if word ~= "" then
    local best = nil
    for i, line in ipairs(lines) do
      suite, name, wildcard = parse_macro(line)
      if suite and name == word then
        local distance = math.abs(i - cursor_line)
        if not best or distance < best.distance then
          best = {
            suite = suite,
            name = name,
            filter = filter_for(suite, name, wildcard),
            distance = distance,
          }
        end
      end
    end
    if best then
      return best
    end
  end

  for i = cursor_line, 1, -1 do
    suite, name, wildcard = parse_macro(lines[i] or "")
    if suite then
      return { suite = suite, name = name, filter = filter_for(suite, name, wildcard) }
    end
  end
end

function M.command_for_filter(kind, filter, opts)
  opts = opts or {}
  local build_dir = opts.build_dir or "build"
  local target = assert(targets[kind], "unknown vimfy test kind: " .. tostring(kind))
  local build_cmd
  if opts.full_build or opts.fast == false then
    build_cmd = cmake_build_cmd(build_dir, target)
  else
    local build_cmds = {}
    for _, prereq in ipairs(fast_build_prereqs) do
      table.insert(build_cmds, cmake_build_cmd(build_dir, prereq .. "/fast"))
    end
    table.insert(build_cmds, cmake_build_cmd(build_dir, target .. "/fast"))
    build_cmd = table.concat(build_cmds, " && ")
  end

  if kind == "debug" then
    return build_cmd .. " && " .. shellescape("./" .. build_dir .. "/tests/vimfy_debug") ..
        " --gtest_filter=" .. shellescape(filter)
  end

  local run_cmd = ""
  if build_dir ~= "build" then
    run_cmd = "VIMFY_BUILD_DIR=" .. shellescape(build_dir) .. " "
  end
  run_cmd = run_cmd .. "scripts/vimfy_tests " .. shellescape(kind) .. " " .. shellescape(filter)
  if (kind == "property" or kind == "safety") and opts.mode then
    run_cmd = run_cmd .. " " .. shellescape(opts.mode)
  end

  return build_cmd .. " && " .. run_cmd
end

function M.run_gtest_here(opts)
  opts = opts or {}
  local test = M.find_test_at_cursor()
  if not test then
    vim.notify("Need cursor near a supported GTest or FuzzTest macro", vim.log.levels.WARN)
    return
  end

  local kind = opts.kind or M.test_kind_for_file()
  local root = opts.repo_root or M.repo_root()
  local cmd = "cd " .. shellescape(root) .. " && " ..
      M.command_for_filter(kind, test.filter, opts)

  vim.cmd("only")
  vim.cmd("botright split | resize " .. tostring(opts.height or 15))
  vim.cmd("terminal " .. cmd)

  local buf = vim.api.nvim_get_current_buf()
  vim.keymap.set({"n", "t"}, "q", function()
    vim.cmd("bdelete!")
  end, { buffer = buf, silent = true })
end

return M
