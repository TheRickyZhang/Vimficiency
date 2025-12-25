local M = {}

local v  = vim.api
local uv = vim.uv
local fs = vim.fs

local types = require("vimficiency.types")

----------- BEGIN FILE ------------
M.basename = fs.basename or function(p)
  return p:match("([^/\\]+)$") or p
end

function M.ensure_dir(p)
  vim.fn.mkdir(p, "p")
end

local function is_file(p)
  local st = uv.fs_stat(p)
  return st ~= nil and st.type == "file"
end

-- ID Generation for file names (Snowflake method)
local function sanitize_filename(s)
  return (s:gsub("[^%w%._%-]", "_"))
end


---@param title string
---@param text? string
---@return integer buf
---@return integer win
function M.show_output(title, text)
  local lines = { title }
  if text and text ~= "" then
    lines[#lines + 1] = ""
    vim.list_extend(lines, vim.split(text, "\n", { plain = true }))
  end

  vim.cmd("botright new") -- create split + window
  local win = v.nvim_get_current_win()

  local buf = v.nvim_create_buf(false, true) -- listed=false, scratch=true
  v.nvim_win_set_buf(win, buf)

  v.nvim_buf_set_name(buf, ("vimficiency://%s"):format(title:gsub("%s+", "_")))
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)

  -- Modern option APIs
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].modifiable = false

  vim.wo[win].number = false
  vim.wo[win].relativenumber = false
  vim.wo[win].wrap = false
  vim.wo[win].signcolumn = "no"

  return buf, win
end


-- Snowflake-like: wall-clock milliseconds + per-ms sequence
local last_ms = -1
local seq = 0
local SEQ_BITS = 12
local SEQ_MAX = (2 ^ SEQ_BITS) - 1

local function now_ms()
  local sec, usec = uv.gettimeofday()
  local ms = sec * 1000 + math.floor(usec / 1000)
  return ms, sec
end

local function wait_next_ms(prev_ms)
  local ms, sec = now_ms()
  while ms == prev_ms do
    ms, sec = now_ms()
  end
  return ms, sec
end

function M.new_id(buf)
  assert(type(buf) == "number", "buf must be a number")
  assert(v.nvim_buf_is_valid(buf), "invalid buffer")

  local name = v.nvim_buf_get_name(buf)
  local base = sanitize_filename(
    (name ~= "" and M.basename(name)) or "NoName"
  )

  local ms, sec = now_ms()

  -- Clamp if clock goes backwards
  if ms < last_ms then
    ms = last_ms
    sec = math.floor(ms / 1000)
  end

  if ms == last_ms then
    -- Same millisecond as previous id
    if seq < SEQ_MAX then
      seq = seq + 1
    else
      -- Overflow: wait for next millisecond
      ms, sec = wait_next_ms(last_ms)
      last_ms = ms
      seq = 0
    end
  else
    -- New millisecond
    last_ms = ms
    seq = 0
  end

  local wall = os.date("%Y%m%d-%H%M%S", sec)
    .. string.format("-%03d", ms % 1000)

  -- Example: foo_cpp__20251222-153045-123__1734888645123__0001__b3
  return string.format("%s__%s__%d__%04d__b%d", base, wall, ms, seq, buf)
end
----------- BEGIN FILE ------------





-- ---------- State / IO helpers ----------
---@return VimficiencyFileContents
function M.capture_state(buf, win)
  -- Precondition: must pass in valid buffer and window
  assert(buf and win, "capture_state: buf and win required")
  assert(v.nvim_buf_is_valid(buf), "capture state: invalid buffer")
  assert(v.nvim_win_is_valid(win), "capture state: invalid window")
  assert(v.nvim_win_get_buf(win) == buf, "capture state: buffer not in window")

  local lines = v.nvim_buf_get_lines(buf, 0, -1, false)
  local cursor = v.nvim_win_get_cursor(win) -- {row(1-based), col(0-based)}

  return types.new_file_contents(
    v.nvim_buf_get_name(buf),
    vim.bo[buf].filetype,
    cursor[1] - 1, --- Convert to 0-indexed, annoyingly
    cursor[2],
    lines
  );
end

---@param path string
---@param obj VimficiencyFileContents
function M.write_vimficiency(path, obj)
  -- Plain-text format:
  -- line 1: "vimficiency" version(int)
  -- line 2: <filetype>
  -- line 3: <bufname>
  -- line 4: <row> <col>
  -- line 5+: buffer lines
  local out = {}

  table.insert(out, "vimficiency 1")
  table.insert(out, obj.filetype or "")
  table.insert(out, obj.bufname or "")
  table.insert(out, string.format("%d %d", obj.row, obj.col))

  for _, line in ipairs(obj.lines or {}) do
    table.insert(out, line)
  end

  vim.fn.writefile(out, path)
  print("successfully wrote to " .. path)
end

---@return VimficiencyFileContents
function M.read_vimficiency(path)
  local st = uv.fs_stat(path)
  if not st or st.type ~= "file" then
    error(("read_json: missing file: %s"):format(path))
  end

  local ok, lines = pcall(vim.fn.readfile, path)
  if not ok then
    error(("read_json: read failed (%s): %s"):format(path, lines))
  end

  if type(lines) ~= "table" or #lines < 4 then
    error(("read_json: snapshot truncated or invalid: %s"):format(path))
  end

  local header = lines[1]
  if type(header) ~= "string" then
    error(("read_json: header not a string in %s"):format(path))
  end

  -- header: "vimficiency 1"
  local magic, version_str = header:match("^(%S+)%s+(%d+)$")
  if not magic or not version_str then
    error(("read_json: bad snapshot header: %q in %s"):format(header, path))
  end
  if magic ~= "vimficiency" then
    error(("read_json: wrong magic %q in %s"):format(magic, path))
  end

  local version = tonumber(version_str)
  if version ~= 1 then
    error(("read_json: unsupported snapshot version %d in %s"):format(version or -1, path))
  end

  local filetype = lines[2]
  local bufname  = lines[3]

  if type(filetype) ~= "string" then
    error(("read_json: filetype line not a string in %s"):format(path))
  end
  if type(bufname) ~= "string" then
    error(("read_json: bufname line not a string in %s"):format(path))
  end

  local coord_line = lines[4]
  if type(coord_line) ~= "string" then
    error(("read_json: coord line not a string in %s"):format(path))
  end

  local row_str, col_str = coord_line:match("^(%S+)%s+(%S+)$")
  if not row_str or not col_str then
    error(("read_json: bad coord line %q in %s"):format(coord_line, path))
  end

  local function to_integer(v_str, name)
    local n = tonumber(v_str)
    if not n then
      error(("read_json: %s %q is not a number in %s"):format(name, v_str, path))
    end
    if n ~= math.floor(n) then
      error(("read_json: %s %q is not an integer in %s"):format(name, v_str, path))
    end
    return n
  end

  local row = to_integer(row_str, "row")
  local col = to_integer(col_str, "col")

  local body = {}
  for i = 5, #lines do
    local line = lines[i]
    if type(line) ~= "string" then
      error(("read_json: line %d not a string in %s"):format(i, path))
    end
    body[#body+1] = line
  end

  return types.new_file_contents(bufname, filetype, row, col, body)
end


---@param key_seq VimficiencyKeyEvent[]
function M.run_program(exe, start_path, end_path, key_seq)
  assert(type(exe) == "string", "need exe string")
  assert(is_file(start_path) and is_file(end_path), "start and end paths must be valid")

  local parts = {}
  for i=1,#key_seq do
    parts[#parts+1] = key_seq[i].key_typed
  end
  local user_seq = table.concat(parts, "")

  local cmd = { exe, start_path, end_path, user_seq }
  print("executing command: " .. vim.inspect(cmd))

  if not vim.system then
    error("cannot find neovim version 10+")
    return nil, nil, nil
  end

  local obj = vim.system(cmd, { text = true }):wait()
  local code = obj.code or 1
  local signal = obj.signal or 0
  local stdout = obj.stdout or ""
  local stderr = obj.stderr or ""
  --
  -- Treat "normal" only as: exited with code 0 and no signal.
  local ok = (code == 0 and signal == 0)

  if not ok then
    error((
      "vimficiency exe failed (code=%s, signal=%s)\nstdout:\n%s\nstderr:\n%s"
    ):format(tostring(code), tostring(signal), stdout, stderr))
  end

  return 0, stdout, stderr
end

return M
