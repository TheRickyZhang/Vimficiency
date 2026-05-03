-- lua/vimficiency/ffi.lua

local ffi = require("ffi")

local M = {}

local function find_plugin_root()
	local source = debug.getinfo(1, "S").source
	if source:sub(1, 1) == "@" then
		source = source:sub(2) -- remove leading @
	end
	return vim.fn.fnamemodify(source, ":h:h:h")
end

-- Read Api.def as raw bytes (not via vim.fn.readfile, which loses trailing
-- newline / line-ending nuance) so vim.fn.sha256() of the result matches
-- CMake's file(SHA256 ...) of the same file byte-for-byte. The ABI check in
-- load_lib() relies on this equivalence.
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

-- FFI type annotations for LuaLS. These mirror `src/LuaExports/Api.def`;
-- Api.def is the canonical C ABI declaration consumed by both C++ and LuaJIT.

---@class VF.C.ScoreWeights
---@field keyWeight number
---@field sameFingerWeight number
---@field sameKeyWeight number
---@field altHandWeight number
---@field goodRollWeight number
---@field badRollWeight number

---@class VF.C.KeyInfo
---@field hand integer
---@field finger integer
---@field base_cost number

---@class VF.C.CountPenaltyOverride
---@field has_base boolean
---@field base number
---@field has_count_slope boolean
---@field count_slope number
---@field has_span_slope boolean
---@field span_slope number

---@class VF.C.Config
---@field default_keyboard integer
---@field weights VF.C.ScoreWeights
---@field keys VF.C.KeyInfo[]
---@field slice_buffer_amount integer
---@field shiftwidth integer  -- -1 = use default (8)
---@field use_count_penalty_overrides boolean
---@field count_penalty_overrides VF.C.CountPenaltyOverride[]

---@class VF.C.Lib
---@field VF_KEY_COUNT integer
---@field VF_FINGER_COUNT integer
---@field VF_HAND_COUNT integer
---@field VF_COUNT_CLASS_COUNT integer
---@field vf_key_name fun(index: integer): ffi.cdata*
---@field vf_finger_name fun(index: integer): ffi.cdata*
---@field vf_hand_name fun(index: integer): ffi.cdata*
---@field vf_count_class_name fun(index: integer): ffi.cdata*
---@field vf_get_config fun(): VF.C.Config
---@field vf_apply_config fun(): nil
---@field vf_analyze fun(initial_text: string, goal_text: string, boundary_first_col: integer, boundary_last_col: integer, has_lines_above: boolean, has_lines_below: boolean, start_row: integer, start_col: integer, end_row: integer, end_col: integer, keyseq: string, window_height: integer, scroll_amount: integer, results_calculated: integer, optimizer_overrides: string): string
---@field vf_get_debug fun(): string
---@field vf_get_warnings fun(): string
---@field vf_get_optimizer_defaults fun(): string
---@field vf_version fun(): integer
---@field vf_abi_hash fun(): ffi.cdata*
---@field vf_debug_config fun(): string
---@field vf_tokenize_movements fun(seq: string): string
---@field vf_tokenize_sequence fun(seq: string): string
---@field vf_build_sequence fun(encoded_events: string): string
---@field vf_compute_search_region fun(encoded_start_lines: string, encoded_end_lines: string, start_row: integer, end_row: integer, padding: integer): string
---@field vf_resolve_recall_cutoff fun(encoded_records: string, target_hrtime: integer, budget: integer): integer
---@field vf_manual_evict_reason fun(start_row: integer, cursor_row: integer, last_key_time_ns: integer, has_last_key: boolean, now_ns: integer, max_search_lines: integer, manual_idle_timeout_seconds: integer): integer
---@field vf_format_sequence fun(seq: string): string
---@field vf_simulate_movements fun(encoded_lines: string, start_row: integer, start_col: integer, seq: string): string
---@field vf_explore_start fun(encoded_initial_lines: string, start_row: integer, start_col: integer, encoded_goal_lines: string, end_row: integer, end_col: integer, boundary_first_col: integer, boundary_last_col: integer, has_lines_above: boolean, has_lines_below: boolean, window_height: integer, scroll_amount: integer, user_seq: string): string
---@field vf_explore_destroy fun(view_id: integer): integer
---@field vf_explore_state fun(view_id: integer): string
---@field vf_explore_recommendations fun(view_id: integer, max_count: integer, optimizer_overrides: string): string
---@field vf_explore_apply_movement fun(view_id: integer, movement_text: string): string
---@field vf_explore_accept_cursor_move fun(view_id: integer, new_row: integer, new_col: integer, raw_keys: string): string
---@field vf_explore_apply_edit fun(view_id: integer, text: string): string
---@field vf_explore_accept_buffer_state fun(view_id: integer, encoded_lines: string, new_row: integer, new_col: integer, raw_keys: string): string
---@field vf_explore_current_lines fun(view_id: integer): string
---@field vf_explore_begin_insert fun(view_id: integer): string
---@field vf_explore_accept_insert_exit fun(view_id: integer, encoded_lines: string, new_row: integer, new_col: integer, raw_keys: string): string
---@field vf_explore_cancel_insert fun(view_id: integer): string
---@field vf_explore_undo fun(view_id: integer): string
---@field vf_explore_redo fun(view_id: integer): string

local api_def_text = read_ffi_api_def()
ffi.cdef(api_def_text)

--- Map jit.os to (filename, system-name) for the shared library. CMake's
--- `add_library(... SHARED ...)` produces `libvimficiency.so` on Linux/BSD,
--- `libvimficiency.dylib` on macOS, and `vimficiency.dll` on Windows.
---@return string filename
---@return string system_name
local function lib_filenames()
	local os_name = jit and jit.os or ""
	if os_name == "OSX" then
		return "libvimficiency.dylib", "vimficiency"
	elseif os_name == "Windows" then
		return "vimficiency.dll", "vimficiency"
	else -- Linux, BSD, Other
		return "libvimficiency.so", "vimficiency"
	end
end

--- Find and load the shared library.
--- `VIMFICIENCY_LIB_PATH` overrides the default build path for tests.
--- `_G.__vimficiency_reload_lib_path` lets `:Vimfy reload` bypass dlopen caching.
---@return VF.C.Lib
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
	table.insert(paths, root .. "/build/" .. lib_filename) -- local build
	table.insert(paths, system_name) -- system path

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

--- Produces: t["Q"] = 0, t["W"] = 1, etc.
local function build_enum(count, name_fn)
	local t = {}
	for i = 0, count - 1 do
		local name = ffi.string(name_fn(i))
		t[name] = i
	end
	return t
end

---@type VF.C.Lib
local lib = load_lib()

M.Key = build_enum(lib.VF_KEY_COUNT, lib.vf_key_name)
M.Finger = build_enum(lib.VF_FINGER_COUNT, lib.vf_finger_name)
M.Hand = build_enum(lib.VF_HAND_COUNT, lib.vf_hand_name)
M.CountClass = build_enum(lib.VF_COUNT_CLASS_COUNT, lib.vf_count_class_name)

local EVENT_FIELD_SEP = string.char(0x1f)
local EVENT_RECORD_SEP = string.char(0x1e)

---@param items string[]
---@return string
local function encode_string_list(items)
	local out = {}
	for i = 1, #items do
		local item = items[i]
		out[#out + 1] = tostring(#item)
		out[#out + 1] = ":"
		out[#out + 1] = item
	end
	return table.concat(out)
end

--- Encode a Lua number as a decimal integer string with NO scientific
--- notation, for int64 payloads crossing the FFI boundary. `tostring(n)`
--- for large hrtime-scale values produces `"1.07e+14"`, which the C++
--- `from_chars<int64_t>` parser rejects, silently reducing to a zero
--- return (see the `resolve_recall_cutoff` bug fix). Use this helper at
--- every FFI encode site carrying an hrtime/time_ns-style value.
---
--- Known limitation: Lua's double-precision floats lose bit-level
--- accuracy above 2^53 (~104 days of ns uptime). For the comparisons
--- this helper serves, relative drift is fine; for anything that needs
--- true int64 identity, prefer LuaJIT `ffi.new("int64_t", n)` and pass
--- cdata directly (not implemented here yet — see architectural note
--- in the bug-fix commit).
---@param n number
---@return string
local function encode_int64(n)
	return string.format("%.0f", n)
end

---@param encoded string
---@return string[]
local function decode_string_list(encoded)
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

---@param key_seq VF.KeyEvent[]
---@return string
function M.build_sequence(key_seq)
	local parts = {}
	for i = 1, #key_seq do
		local ev = key_seq[i]
		-- `key_typed` is always `keytrans()` output (printable `<...>` form,
		-- e.g. `<C-_>`, never raw 0x1f/0x1e). An invariant violation here
		-- means a caller bypassed keytrans, not a runtime condition we
		-- should handle — assert rather than escape/length-prefix.
		assert(not ev.key_typed:find("[\x1e\x1f]"),
			"build_sequence: key_typed contains a record/field separator; " ..
			"callers must pass keytrans'd strings")
		parts[#parts + 1] = ev.mode
		parts[#parts + 1] = EVENT_FIELD_SEP
		parts[#parts + 1] = ev.key_typed
		parts[#parts + 1] = EVENT_RECORD_SEP
	end
	local result = ffi.string(lib.vf_build_sequence(table.concat(parts)))
	if result:sub(1, 7) == "ERROR: " then
		error("vf_build_sequence failed: " .. result, 0)
	end
	return result
end

---@param start_lines string[]
---@param end_lines string[]
---@param start_row integer
---@param end_row integer
---@param padding integer
---@return integer
---@return integer
function M.compute_search_region(start_lines, end_lines, start_row, end_row, padding)
	local result = ffi.string(lib.vf_compute_search_region(
		encode_string_list(start_lines),
		encode_string_list(end_lines),
		start_row,
		end_row,
		padding
	))
	if result:sub(1, 7) == "ERROR: " then
		error("vf_compute_search_region failed: " .. result, 0)
	end
	local parts = vim.split(result, EVENT_FIELD_SEP, { plain = true, trimempty = true })
	assert(#parts == 2, "vf_compute_search_region returned malformed payload")
	local a = assert(tonumber(parts[1]), "vf_compute_search_region: non-numeric region start")
	local b = assert(tonumber(parts[2]), "vf_compute_search_region: non-numeric region end")
	return a, b
end

---@param records table<string, { time_started: integer, first_mode: string|nil }>
---@param order string[]
---@param target_hrtime integer
---@param budget integer
---@return integer|nil
function M.resolve_recall_cutoff(records, order, target_hrtime, budget)
	local parts = {}
	for i = 1, #order do
		local rec = records[order[i]]
		assert(rec, "resolve_recall_cutoff: missing record for order index " .. i)
		parts[#parts + 1] = encode_int64(rec.time_started)
		parts[#parts + 1] = rec.first_mode or ""
	end
	local index = lib.vf_resolve_recall_cutoff(
		encode_string_list(parts),
		target_hrtime,
		budget
	)
	if index == 0 then
		return nil
	end
	return index
end

---@param start_row integer
---@param cursor_row integer
---@param last_key_time_ns integer|nil
---@param now_ns integer
---@param max_search_lines integer
---@param manual_idle_timeout_seconds integer
---@return string|nil
function M.manual_evict_reason(
	start_row,
	cursor_row,
	last_key_time_ns,
	now_ns,
	max_search_lines,
	manual_idle_timeout_seconds
)
	local has_last_key = last_key_time_ns ~= nil
	local code = lib.vf_manual_evict_reason(
		start_row,
		cursor_row,
		last_key_time_ns or 0,
		has_last_key,
		now_ns,
		max_search_lines,
		manual_idle_timeout_seconds
	)
	if code == 1 then
		return "cursor drifted beyond MAX_SEARCH_LINES (" .. max_search_lines .. ")"
	end
	if code == 2 then
		return "idle for more than " .. manual_idle_timeout_seconds .. "s"
	end
	return nil
end

-- The (has_<name>, <name>) flag pairs on VF.C.CountPenaltyOverride. Genuine
-- structure (not mere field duplication), so still a named schema.
local OVERRIDE_FIELDS = { "base", "count_slope", "span_slope" }

--- Try to assign a value to a cdata field, distinguishing unknown fields
--- from type errors. LuaJIT raises on both reads and writes of absent
--- struct members, so a read-probe tells us whether the field exists.
---@return "ok"|"unknown"|"type_error" status
---@return string|nil err  Raw LuaJIT error when status == "type_error"
local function try_assign(cdata, key, value)
	if not pcall(function() local _ = cdata[key] end) then
		return "unknown"
	end
	local ok, err = pcall(function() cdata[key] = value end)
	if not ok then
		return "type_error", err
	end
	return "ok"
end

--- Apply scalar user overrides to a cdata struct. Returns a set of keys that
--- were actually claimed; raises on type mismatches.
---
--- When `strict` is true, unknown fields also raise (so nested typos like
--- `weights = { downwrad = 5 }` fail loudly instead of silently no-op'ing).
--- Top-level calls pass `strict = false` because setup() does its own
--- post-check against the union of lua+cpp consumed keys.
local function apply_scalars(src, dst, key_prefix, strict)
	local consumed = {}
	for k, v in pairs(src) do
		if type(v) ~= "table" then
			local status, err = try_assign(dst, k, v)
			if status == "ok" then
				consumed[k] = true
			elseif status == "type_error" then
				error(string.format("vimfy: invalid value for '%s%s': %s",
					key_prefix or "", tostring(k), tostring(err)))
			elseif strict and status == "unknown" then
				error(string.format("vimfy: unknown config key '%s%s'",
					key_prefix or "", tostring(k)))
			end
		end
	end
	return consumed
end

-- Allowed keys on a single count_penalty_overrides[class] entry. Mirrors
-- OVERRIDE_FIELDS; a set form lets us validate keys in O(1).
local OVERRIDE_FIELD_SET = { base = true, count_slope = true, span_slope = true }

-- ---@param user_config VF.C.Config
---@return table<string, true> consumed  Top-level user_config keys claimed by C++ side
function M.configure(user_config)
	---@type VF.C.Config
	local config = lib.vf_get_config()

	local consumed = apply_scalars(user_config, config)

	if user_config.weights then
		consumed.weights = true
		apply_scalars(user_config.weights, config.weights, "weights.", true)
	end

	if user_config.keys then
		consumed.keys = true
		for key_index, info in pairs(user_config.keys) do
			config.keys[key_index].hand = info.hand
			config.keys[key_index].finger = info.finger
			config.keys[key_index].base_cost = info.cost
		end
	end

	if user_config.count_penalty_overrides then
		consumed.count_penalty_overrides = true
		if user_config.use_count_penalty_overrides == nil then
			config.use_count_penalty_overrides = true
		end

		-- Reset all has_* flags before applying user's subset.
		for i = 0, lib.VF_COUNT_CLASS_COUNT - 1 do
			local dst = config.count_penalty_overrides[i]
			for _, f in ipairs(OVERRIDE_FIELDS) do
				dst["has_" .. f] = false
			end
		end

		for class_key, override in pairs(user_config.count_penalty_overrides) do
			local class_index
			if type(class_key) == "number" then
				class_index = class_key
			else
				class_index = M.CountClass[class_key]
			end

			if class_index == nil then
				error("Unknown count penalty class: " .. tostring(class_key))
			end
			if class_index < 0 or class_index >= lib.VF_COUNT_CLASS_COUNT then
				error("Count penalty class out of range: " .. tostring(class_key))
			end

			for field in pairs(override) do
				if not OVERRIDE_FIELD_SET[field] then
					error(string.format(
						"vimfy: unknown count_penalty_overrides[%s] key '%s' (allowed: base, count_slope, span_slope)",
						tostring(class_key), tostring(field)))
				end
			end

			local dst = config.count_penalty_overrides[class_index]
			for _, f in ipairs(OVERRIDE_FIELDS) do
				if override[f] ~= nil then
					dst["has_" .. f] = true
					dst[f] = override[f]
				end
			end
		end
	end

	lib.vf_apply_config()
	return consumed
end

--- Parse the C++ analyze-export payload into {results, user_cost}.
--- Extracted so the parser can be unit-tested directly. Format produced by
--- `src/LuaExports/AnalyzeExports.cpp`:
---
---   size: N user_cost: X.XXX
---   <raw_seq_bytes>\x1F<cost>\n
---   ...
---   ----------------DEBUG---------------- (optional trailer)
---
--- Convention 3 in `dev/lua/ffi-separators.md`: newline-separated records,
--- each body record framed with `kEventFieldSep` (0x1F) between the raw
--- sequence bytes and the numeric cost. Do not change the separator here
--- without also updating the constant in `src/LuaExports/Shared.h`.
---@param result_str string
---@return VF.Optimizer.Result[] results, number user_cost
local function parse_analyze_results(result_str)
  ---@type VF.Optimizer.Result[]
  local results = {}
  local user_cost = 0
  local line_num = 0
  for line in result_str:gmatch("[^\n]+") do
    if line:find("----------------DEBUG----------------", 1, true) then
      break
    end
    line_num = line_num + 1
    if line_num == 1 then
      local cost_str = line:match("user_cost:%s*(%S+)")
      if cost_str then
        user_cost = tonumber(cost_str) or 0
      end
    else
      local seq, cost_str = line:match("^(.*)\x1F(%S+)$")
      if seq then
        table.insert(results, {
          seq = seq,
          cost = tonumber(cost_str) or 0,
        })
      end
    end
  end
  return results, user_cost
end

M._parse_analyze_results = parse_analyze_results

---@param initial_lines string[] Buffer lines at session start
---@param goal_lines string[] Buffer lines at session end (can equal initial_lines for motion-only)
---@param boundary_first_col integer Column offset at start of boundary (0 for linewise)
---@param boundary_last_col integer Last valid column in boundary (lastLineLen-1 for linewise)
---@param has_lines_above boolean Whether there are lines above the slice in the full buffer
---@param has_lines_below boolean Whether there are lines below the slice in the full buffer
---@param start_row integer (0-indexed, relative to slice)
---@param start_col integer (0-indexed)
---@param end_row integer (0-indexed, relative to slice)
---@param end_col integer (0-indexed)
---@param key_seq string
---@param window_height integer
---@param scroll_amount integer
---@class VF.OptimizerOverrides
---@field shared? table<string, number|integer|boolean>
---@field nav? table<string, number|integer|boolean>
---@field transform? table<string, number|integer|boolean>
---@field composition? table<string, number|integer|boolean>

---Encode optimizer-param overrides for the FFI boundary. Each scope's
---table is flattened to `<scope>:<key>=<value>` lines; booleans become
---`0` / `1`. Empty/nil input yields the empty string (= use C++ defaults).
---Mirror of `OptimizerParamOverrides::parse` on the C++ side.
---@param overrides? VF.OptimizerOverrides
---@return string
function M.encode_optimizer_overrides(overrides)
  if not overrides then return "" end
  local lines = {}
  for _, scope in ipairs({ "shared", "nav", "transform", "composition" }) do
    local kvs = overrides[scope]
    if kvs then
      for k, v in pairs(kvs) do
        local encoded
        if type(v) == "boolean" then
          encoded = v and "1" or "0"
        else
          encoded = tostring(v)
        end
        lines[#lines + 1] = scope .. ":" .. k .. "=" .. encoded
      end
    end
  end
  return table.concat(lines, "\n")
end

---Fetch the C++ optimizer parameter defaults as a typed nested table.
---Cached for the lifetime of the LuaJIT process — the C++ side caches
---the encoded string and these defaults are compile-time constants.
---Wire format mirrors the override blob with an extra `:type` token:
---   `<scope>:<key>:int=10`
---   `<scope>:<key>:double=2.0`
---   `<scope>:<key>:bool=1`
---@return { nav: table<string, any>, transform: table<string, any>, composition: table<string, any> }
local optimizer_defaults_cache = nil
function M.get_optimizer_defaults()
  if optimizer_defaults_cache ~= nil then return optimizer_defaults_cache end
  local raw = ffi.string(lib.vf_get_optimizer_defaults())
  local out = { nav = {}, transform = {}, composition = {} }
  for line in string.gmatch(raw, "[^\n]+") do
    local scope, key, typ, value = line:match("^([^:]+):([^:]+):([^=]+)=(.*)$")
    if scope and out[scope] then
      if typ == "int" then
        out[scope][key] = tonumber(value)
      elseif typ == "double" then
        out[scope][key] = tonumber(value)
      elseif typ == "bool" then
        out[scope][key] = (value == "1" or value == "true")
      else
        out[scope][key] = value  -- unknown type — store raw
      end
    end
  end
  optimizer_defaults_cache = out
  return out
end

---@param RESULTS_CALCULATED integer
---@param optimizer_overrides? string Newline-delimited `<scope>:<key>=<value>` lines; empty/nil for defaults
---@return VF.Optimizer.Result[] results, number user_cost, string debug
function M.analyze(
  initial_lines, goal_lines,
  boundary_first_col, boundary_last_col,
  has_lines_above, has_lines_below,
  start_row, start_col, end_row, end_col,
  key_seq,
  window_height, scroll_amount,
  RESULTS_CALCULATED,
  optimizer_overrides
)
	local initial_text = table.concat(initial_lines, "\n")
	local goal_text = table.concat(goal_lines, "\n")

	local result = lib.vf_analyze(
    initial_text, goal_text,
    boundary_first_col, boundary_last_col,
    has_lines_above, has_lines_below,
    start_row, start_col, end_row, end_col,
    key_seq,
    window_height, scroll_amount,
    RESULTS_CALCULATED,
    optimizer_overrides or ""
  )
  local dbg = ffi.string(lib.vf_get_debug())
  local result_str = ffi.string(result)

  if result_str:sub(1, 6) == "ERROR:" then
    error(result_str)
  end

  ---@class VF.Optimizer.Result
  ---@field seq string Motion sequence
  ---@field cost number Effort cost

  local results, user_cost = parse_analyze_results(result_str)
  return results, user_cost, dbg
end

function M.version()
	return lib.vf_version()
end

function M.debug_config()
	return ffi.string(lib.vf_debug_config())
end

---@class VF.Sequence.Token
---@field text string
---@field kind "movement"|"delete"|"change"|"visual"|"typed"|"escape"

-- Wire kind → Lua kind. Matches `tokenKindChar` in UtilityExports.cpp.
-- Keep in sync: if the C++ enum grows a case, add its char here.
local KIND_CHAR_TO_NAME = {
  M = "movement",
  D = "delete",
  C = "change",
  V = "visual",
  T = "typed",
  E = "escape",
}

--- Parse the `<kind>\t<text>\n` wire format from the C++ tokenizer.
---@param result_str string
---@return VF.Sequence.Token[]
local function parse_kinded_tokens(result_str)
  ---@type VF.Sequence.Token[]
  local out = {}
  -- Each line is `<kind_char>\t<text>`; blank lines (trailing `\n` from
  -- C++) are skipped by the pattern.
  for line in result_str:gmatch("([^\n]+)") do
    local kind = KIND_CHAR_TO_NAME[line:sub(1, 1)]
    -- Skip the separator byte (`\t`); token text starts at byte 3.
    local text = line:sub(3)
    out[#out + 1] = { text = text, kind = kind or "movement" }
  end
  return out
end

--- Tokenize a movement sequence into kinded tokens. Handles counts, f/F/t/T
--- + target char, and special keys like `<C-d>`. All returned tokens have
--- `kind = "movement"` (the movement parser doesn't emit other kinds).
---@param seq string  Movement sequence (e.g., "3wfx;j")
---@return VF.Sequence.Token[] tokens
---@return string|nil error
function M.tokenize_movements(seq)
  if not seq or seq == "" then return {}, nil end
  local result_str = ffi.string(lib.vf_tokenize_movements(seq))
  if result_str == "" then return {}, nil end
  if result_str:sub(1, 6) == "ERROR:" then return {}, result_str end
  return parse_kinded_tokens(result_str), nil
end

--- Tokenize a full Vim sequence (motions, edits, visual, insert-mode text)
--- into kinded tokens. Tokens carry modal-transition metadata so the Lua
--- animation layer doesn't maintain a parallel classifier.
---@param seq string  Vim sequence (e.g., "ciwhello<Esc>2j")
---@return VF.Sequence.Token[] tokens
---@return string|nil error
function M.tokenize_sequence(seq)
  if not seq or seq == "" then return {}, nil end
  local result_str = ffi.string(lib.vf_tokenize_sequence(seq))
  if result_str == "" then return {}, nil end
  if result_str:sub(1, 6) == "ERROR:" then return {}, result_str end
  return parse_kinded_tokens(result_str), nil
end

--- Format a sequence string for human-readable display
--- Tokenizes into logical units and joins with spaces
---@param seq string Vim sequence (e.g., "3rx<C-d>ciwfoo<Esc>")
---@return string Formatted sequence (e.g., "3rx <C-d> ciw foo <Esc>")
function M.format_sequence(seq)
  if not seq or seq == "" then
    return ""
  end
  return ffi.string(lib.vf_format_sequence(seq))
end

---@param lines string[]
---@param start_row integer
---@param start_col integer
---@param seq string
---@return integer|nil row
---@return integer|nil col
---@return string|nil err
function M.simulate_movements(lines, start_row, start_col, seq)
  if not seq or seq == "" then
    return start_row, start_col, nil
  end
  local result_str = ffi.string(lib.vf_simulate_movements(
    encode_string_list(lines),
    start_row,
    start_col,
    seq
  ))
  if result_str:sub(1, 6) == "ERROR:" then
    return nil, nil, result_str
  end
  local parts = vim.split(result_str, EVENT_FIELD_SEP, { plain = true, trimempty = true })
  assert(#parts == 2, "vf_simulate_movements returned malformed payload")
  return tonumber(parts[1]), tonumber(parts[2]), nil
end

---@class VF.Position
---@field row integer  # 0-indexed
---@field col integer  # 0-indexed

---@class VF.Range
---@field begin_pos VF.Position  # inclusive
---@field end_pos VF.Position    # exclusive (half-open)

--- Phase variants. C++ side is `std::variant<Navigate, Transform, Insert>`,
--- each carrying an `int index`. Lua mirrors that via discriminated union —
--- narrow on `phase.kind`.
---  - Navigate(i): i ∈ [0, total_edits]; i == total_edits is the post-final-
---    edit nav segment (target = goal cursor). Pure-motion sessions use
---    Navigate(0) with total_edits == 0.
---  - Transform(i), Insert(i): i ∈ [0, total_edits).
--- Completion is the separate `is_completed` boolean on the state — not a
--- distinct phase variant.
---@class VF.Explore.PhaseNavigate
---@field kind "Navigate"
---@field edit_index integer

---@class VF.Explore.PhaseTransform
---@field kind "Transform"
---@field edit_index integer

---@class VF.Explore.PhaseInsert
---@field kind "Insert"
---@field edit_index integer

---@alias VF.Explore.Phase VF.Explore.PhaseNavigate | VF.Explore.PhaseTransform | VF.Explore.PhaseInsert

---@class VF.Explore.State
---@field phase VF.Explore.Phase
---@field is_completed boolean  # cursor at goalPos with all edits applied
---@field cursor VF.Position
---@field total_edits integer
---@field cost number
---@field seq string
---@field can_undo boolean
---@field can_redo boolean
---@field target_range VF.Range  # half-open diff range for the current phase

--- Apply-result variants. C++ side is `std::expected<void, Rejected>`;
--- Lua mirrors that via discriminated union — narrow on `result.status`.
--- The new view state is fetched separately via `explore_state` after every
--- action, so the apply result carries only success/failure + reason.
---@class VF.Explore.AppliedResult
---@field status "Applied"

---@class VF.Explore.RejectedResult
---@field status "Rejected"
---@field reason string

---@alias VF.Explore.ApplyResult VF.Explore.AppliedResult | VF.Explore.RejectedResult

--- Single uniform recommendation shape. The "kind" of a recommendation
--- (motion / edit-structural / typed-text) is implicit in the view's
--- current phase: Navigate emits motions, Transform emits edit atoms,
--- Insert emits the canonical typed text. Consumers branch on
--- `state.phase.kind` rather than a per-rec discriminator.
---
--- `cost_diff` is the incremental keyboard effort from the accepted sequence.
---@class VF.Explore.Recommendation
---@field text string
---@field cost_diff number
---@field landing VF.Position
---@field rank? integer  # attached on the consumer side after sorting (see explore.lua attach_ranks)

--- Drain any soft-fail diagnostics emitted by the C++ side during the
--- most recent FFI call and surface them via `vim.notify`. Centralized
--- here because every FFI wrapper that returns a payload routes through
--- `require_non_error`, so this catches all entry points uniformly.
local function drain_warnings()
  local pending = ffi.string(lib.vf_get_warnings())
  if pending == "" then return end
  -- pcall: tests / headless contexts may stub `vim.notify`; do not let
  -- a missing notifier turn a soft-fail diagnostic into a hard error.
  pcall(vim.notify, "[vimficiency] " .. pending, vim.log.levels.WARN)
end

---@param payload string
---@return string
local function require_non_error(payload)
  drain_warnings()
  if payload:sub(1, 7) == "ERROR: " then
    error(payload, 0)
  end
  return payload
end

--- Build the kind-discriminated phase from the wire's `(kind, edit_index)`.
---@param kind string
---@param edit_index_str string
---@return VF.Explore.Phase
local function build_phase(kind, edit_index_str)
  -- After Stage 2, edit_index is always a non-negative int. Navigate(i)
  -- carries i ∈ [0, total_edits]; Transform/Insert carry i ∈ [0, total_edits).
  -- Completion is the separate `is_completed` boolean on the state — no
  -- Completed phase variant.
  local edit_index = tonumber(edit_index_str) or 0
  if kind == "Navigate" or kind == "Transform" or kind == "Insert" then
    return { kind = kind, edit_index = edit_index }
  end
  error("unknown phase kind from FFI: " .. tostring(kind))
end

---@param payload string
---@return VF.Explore.State
local function parse_explore_state(payload)
  local parts = decode_string_list(payload)
  assert(#parts == 14, "explore state payload must have 14 fields")
  ---@type VF.Range
  local target_range = {
    begin_pos = { row = tonumber(parts[11]) or 0, col = tonumber(parts[12]) or 0 },
    end_pos = { row = tonumber(parts[13]) or 0, col = tonumber(parts[14]) or 0 },
  }
  return {
    phase = build_phase(parts[1], parts[2]),
    is_completed = parts[3] == "1",
    cursor = { row = tonumber(parts[4]) or 0, col = tonumber(parts[5]) or 0 },
    total_edits = tonumber(parts[6]) or 0,
    cost = tonumber(parts[7]) or 0,
    seq = parts[8],
    can_undo = parts[9] == "1",
    can_redo = parts[10] == "1",
    target_range = target_range,
  }
end

---@param payload string
---@return VF.Explore.ApplyResult
local function parse_explore_apply_result(payload)
  local parts = decode_string_list(payload)
  assert(#parts == 2, "explore apply payload must have 2 fields")
  local status = parts[1]
  if status == "Applied" then
    return { status = "Applied" }
  elseif status == "Rejected" then
    return { status = "Rejected", reason = parts[2] }
  end
  error("unknown apply-result status from FFI: " .. tostring(status))
end

---@param payload string
---@return VF.Explore.Recommendation[]
local function parse_explore_recommendations(payload)
  local parts = decode_string_list(payload)
  assert(#parts >= 1, "explore recommendations payload must have count prefix")
  local count = tonumber(parts[1]) or 0
  local expected = 1 + count * 4
  assert(#parts == expected,
    "explore recommendations payload has " .. #parts ..
    " fields, expected " .. expected .. " for count=" .. count)
  ---@type VF.Explore.Recommendation[]
  local recs = {}
  for i = 1, count do
    local base = 1 + (i - 1) * 4
    recs[i] = {
      text = parts[base + 1],
      cost_diff = tonumber(parts[base + 2]) or 0,
      landing = {
        row = tonumber(parts[base + 3]) or 0,
        col = tonumber(parts[base + 4]) or 0,
      },
    }
  end
  return recs
end

---@param initial_lines string[]
---@param start_row integer
---@param start_col integer
---@param goal_lines string[]
---@param end_row integer
---@param end_col integer
---@param boundary_first_col integer
---@param boundary_last_col integer
---@param has_lines_above boolean
---@param has_lines_below boolean
---@param window_height integer
---@param scroll_amount integer
---@param user_seq string|nil
---@return integer
function M.explore_start(initial_lines, start_row, start_col,
                         goal_lines, end_row, end_col,
                         boundary_first_col, boundary_last_col,
                         has_lines_above, has_lines_below,
                         window_height, scroll_amount,
                         user_seq)
  local payload = require_non_error(ffi.string(lib.vf_explore_start(
    encode_string_list(initial_lines),
    start_row, start_col,
    encode_string_list(goal_lines),
    end_row, end_col,
    boundary_first_col, boundary_last_col,
    has_lines_above, has_lines_below,
    window_height, scroll_amount,
    user_seq or ""
  )))
  return tonumber(payload) or error("explore_start returned non-numeric view_id: " .. payload)
end

---@param view_id integer
---@return boolean
function M.explore_destroy(view_id)
  return lib.vf_explore_destroy(view_id) == 1
end

---@param view_id integer
---@return VF.Explore.State
function M.explore_state(view_id)
  local payload = require_non_error(ffi.string(lib.vf_explore_state(view_id)))
  return parse_explore_state(payload)
end

---@param view_id integer
---@param max_count integer
---@param optimizer_overrides? string Newline-delimited `<scope>:<key>=<value>` lines; empty/nil for defaults. Build via `M.encode_optimizer_overrides`.
---@return VF.Explore.Recommendation[]
function M.explore_recommendations(view_id, max_count, optimizer_overrides)
  local payload = require_non_error(ffi.string(
    lib.vf_explore_recommendations(
      view_id, max_count, optimizer_overrides or "")))
  return parse_explore_recommendations(payload)
end

---@param view_id integer
---@param movement_text string
---@return VF.Explore.ApplyResult
function M.explore_apply_movement(view_id, movement_text)
  local payload = require_non_error(ffi.string(
    lib.vf_explore_apply_movement(view_id, movement_text)))
  return parse_explore_apply_result(payload)
end

---@param view_id integer
---@param new_row integer
---@param new_col integer
---@param raw_keys string|nil
---@return VF.Explore.ApplyResult
function M.explore_accept_cursor_move(view_id, new_row, new_col, raw_keys)
  local payload = require_non_error(ffi.string(lib.vf_explore_accept_cursor_move(
    view_id, new_row, new_col, raw_keys or "")))
  return parse_explore_apply_result(payload)
end

---@param view_id integer
---@param lines string[]
---@param new_row integer
---@param new_col integer
---@param raw_keys string|nil
---@return VF.Explore.ApplyResult
function M.explore_accept_buffer_state(view_id, lines, new_row, new_col, raw_keys)
  local payload = require_non_error(ffi.string(lib.vf_explore_accept_buffer_state(
    view_id, encode_string_list(lines), new_row, new_col, raw_keys or "")))
  return parse_explore_apply_result(payload)
end

---@param view_id integer
---@param text string
---@return VF.Explore.ApplyResult
function M.explore_apply_edit(view_id, text)
  local payload = require_non_error(ffi.string(
    lib.vf_explore_apply_edit(view_id, text)))
  return parse_explore_apply_result(payload)
end

---@param view_id integer
---@return string[]
function M.explore_current_lines(view_id)
  local payload = require_non_error(ffi.string(lib.vf_explore_current_lines(view_id)))
  return decode_string_list(payload)
end

---@param view_id integer
---@return VF.Explore.ApplyResult
function M.explore_begin_insert(view_id)
  local payload = require_non_error(ffi.string(
    lib.vf_explore_begin_insert(view_id)))
  return parse_explore_apply_result(payload)
end

---@param view_id integer
---@param lines string[]
---@param new_row integer
---@param new_col integer
---@param raw_keys string|nil
---@return VF.Explore.ApplyResult
function M.explore_accept_insert_exit(view_id, lines, new_row, new_col, raw_keys)
  local payload = require_non_error(ffi.string(lib.vf_explore_accept_insert_exit(
    view_id, encode_string_list(lines), new_row, new_col, raw_keys or "")))
  return parse_explore_apply_result(payload)
end

---@param view_id integer
---@return VF.Explore.ApplyResult
function M.explore_cancel_insert(view_id)
  local payload = require_non_error(ffi.string(
    lib.vf_explore_cancel_insert(view_id)))
  return parse_explore_apply_result(payload)
end

---@param view_id integer
---@return VF.Explore.ApplyResult
function M.explore_undo(view_id)
  local payload = require_non_error(ffi.string(lib.vf_explore_undo(view_id)))
  return parse_explore_apply_result(payload)
end

---@param view_id integer
---@return VF.Explore.ApplyResult
function M.explore_redo(view_id)
  local payload = require_non_error(ffi.string(lib.vf_explore_redo(view_id)))
  return parse_explore_apply_result(payload)
end

return M
