local ffi = require("ffi")

local M = {}

local function find_plugin_root()
  local source = debug.getinfo(1, "S").source
  if source:sub(1, 1) == "@" then
    source = source:sub(2)
  end
  return vim.fn.fnamemodify(source, ":h:h:h:h")
end

local function read_ffi_api_def()
  local path = find_plugin_root() .. "/src/LuaExports/Api.def"
  local f, err = io.open(path, "rb")
  if not f then
    error("vimfy: failed to read FFI API declarations at " .. path .. ": " .. tostring(err), 0)
  end
  local text = f:read("*all")
  f:close()
  return text
end

local api_def_text = read_ffi_api_def()
ffi.cdef(api_def_text)

---@return string filename
---@return string system_name
local function lib_filenames()
  local os_name = jit and jit.os or ""
  if os_name == "OSX" then
    return "libvimficiency.dylib", "vimficiency"
  elseif os_name == "Windows" then
    return "vimficiency.dll", "vimficiency"
  else
    return "libvimficiency.so", "vimficiency"
  end
end

local function load_lib()
  local root = find_plugin_root()
  local lib_filename, system_name = lib_filenames()
  local paths = {}
  if type(vim.env.VIMFICIENCY_LIB_PATH) == "string"
      and vim.env.VIMFICIENCY_LIB_PATH ~= "" then
    table.insert(paths, vim.env.VIMFICIENCY_LIB_PATH)
  end
  if type(_G.__vimficiency_reload_lib_path) == "string" then
    table.insert(paths, _G.__vimficiency_reload_lib_path)
  end
  table.insert(paths, root .. "/build/" .. lib_filename)
  table.insert(paths, system_name)

  local expected_hash = vim.fn.sha256(api_def_text)

  for _, path in ipairs(paths) do
    local ok, lib = pcall(ffi.load, path)
    if ok then
      local got_hash = ffi.string(lib.vf_abi_hash())
      if got_hash ~= expected_hash then
        error(string.format(
          "vimficiency: ABI mismatch — loaded %s reports Api.def hash %s, " ..
          "but the Lua bindings were authored against %s. The shared library " ..
          "is stale. Rebuild with `cmake --build %s/build` (or `:Lazy build " ..
          "vimficiency` / your plugin manager's equivalent).",
          path, got_hash, expected_hash, root), 0)
      end
      return lib
    end
  end

  error("Could not load " .. lib_filename .. " - tried: " .. table.concat(paths, ", "))
end

local function build_enum(count, name_fn)
  local t = {}
  for i = 0, count - 1 do
    local name = ffi.string(name_fn(i))
    t[name] = i
  end
  return t
end

M.lib = load_lib()

M.Key = build_enum(M.lib.VF_KEY_COUNT, M.lib.vf_key_name)
M.Finger = build_enum(M.lib.VF_FINGER_COUNT, M.lib.vf_finger_name)
M.Hand = build_enum(M.lib.VF_HAND_COUNT, M.lib.vf_hand_name)
M.CountClass = build_enum(M.lib.VF_COUNT_CLASS_COUNT, M.lib.vf_count_class_name)

M.EVENT_FIELD_SEP = string.char(0x1f)
M.EVENT_RECORD_SEP = string.char(0x1e)

---@param slice VF.C.ByteSlice
---@return string
function M.slice_to_string(slice)
  local len = tonumber(slice.len) or 0
  if len == 0 then
    return ""
  end
  return ffi.string(slice.data, len)
end

---@param items string[]
---@return string
function M.encode_string_list(items)
  local out = {}
  for i = 1, #items do
    local item = items[i]
    out[#out + 1] = tostring(#item)
    out[#out + 1] = ":"
    out[#out + 1] = item
  end
  return table.concat(out)
end

---@param lines string[]
---@param name string
---@return string
function M.encode_line_array(lines, name)
  assert(type(lines) == "table", name .. " must be a table")
  assert(#lines > 0, name .. " must contain at least one line")
  for i = 1, #lines do
    local line = lines[i]
    assert(type(line) == "string", name .. "[" .. i .. "] must be a string")
    assert(not line:find("\n", 1, true), name .. "[" .. i .. "] contains newline byte")
    assert(not line:find("\0", 1, true), name .. "[" .. i .. "] contains NUL byte")
  end
  return M.encode_string_list(lines)
end

---@param n number
---@return string
function M.encode_int64(n)
  return string.format("%.0f", n)
end

---@param encoded string
---@return string[]
function M.decode_string_list(encoded)
  local out = {}
  local i = 1
  while i <= #encoded do
    local colon = encoded:find(":", i, true)
    assert(colon, "decode_string_list: missing colon")
    local len = tonumber(encoded:sub(i, colon - 1))
    assert(len and len >= 0, "decode_string_list: invalid length")
    local start = colon + 1
    local stop = start + len - 1
    out[#out + 1] = encoded:sub(start, stop)
    i = stop + 1
  end
  return out
end

function M.version()
  return M.lib.vf_version()
end

function M.debug_config()
  return M.slice_to_string(M.lib.vf_debug_config())
end

return M
