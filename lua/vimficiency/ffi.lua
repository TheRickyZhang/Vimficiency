-- lua/vimficiency/ffi.lua

local ffi = require("ffi")

local M = {}

-- FFI type annotations for LuaLS. These mirror the structs in `ffi.cdef`.

---@class C_ScoreWeights
---@field keyWeight number
---@field sameFingerWeight number
---@field sameKeyWeight number
---@field altHandWeight number
---@field goodRollWeight number
---@field badRollWeight number

---@class C_KeyInfo
---@field hand integer
---@field finger integer
---@field base_cost number

---@class C_CountPenaltyOverride
---@field has_base boolean
---@field base number
---@field has_count_slope boolean
---@field count_slope number
---@field has_span_slope boolean
---@field span_slope number

---@class VimficiencyConfigFFI
---@field default_keyboard integer
---@field weights C_ScoreWeights
---@field keys C_KeyInfo[]
---@field slice_buffer_amount integer
---@field shiftwidth integer  -- -1 = use default (8)
---@field use_count_penalty_overrides boolean
---@field count_penalty_overrides C_CountPenaltyOverride[]

---@class VimficiencyLib
---@field VIMFICIENCY_KEY_COUNT integer
---@field VIMFICIENCY_FINGER_COUNT integer
---@field VIMFICIENCY_HAND_COUNT integer
---@field VIMFICIENCY_COUNT_CLASS_COUNT integer
---@field vimficiency_key_name fun(index: integer): ffi.cdata*
---@field vimficiency_finger_name fun(index: integer): ffi.cdata*
---@field vimficiency_hand_name fun(index: integer): ffi.cdata*
---@field vimficiency_count_class_name fun(index: integer): ffi.cdata*
---@field vimficiency_get_config fun(): VimficiencyConfigFFI
---@field vimficiency_apply_config fun(): nil
---@field vimficiency_analyze fun(initial_text: string, goal_text: string, boundary_first_col: integer, boundary_last_col: integer, has_lines_above: boolean, has_lines_below: boolean, start_row: integer, start_col: integer, end_row: integer, end_col: integer, keyseq: string, window_height: integer, scroll_amount: integer, results_calculated: integer): string
---@field vimficiency_get_debug fun(): string
---@field vimficiency_version fun(): integer
---@field vimficiency_debug_config fun(): string
---@field vimficiency_tokenize_motions fun(seq: string): string
---@field vimficiency_tokenize_sequence fun(seq: string): string
---@field vimficiency_build_sequence fun(encoded_events: string): string
---@field vimficiency_compute_search_region fun(encoded_start_lines: string, encoded_end_lines: string, start_row: integer, end_row: integer, padding: integer): string
---@field vimficiency_resolve_recall_cutoff fun(encoded_records: string, target_hrtime: integer, budget: integer): integer
---@field vimficiency_manual_evict_reason fun(start_row: integer, cursor_row: integer, last_key_time_ns: integer, has_last_key: boolean, now_ns: integer, max_search_lines: integer, manual_idle_timeout_seconds: integer): integer

ffi.cdef([[
    extern const int VIMFICIENCY_KEY_COUNT;
    extern const int VIMFICIENCY_FINGER_COUNT;
    extern const int VIMFICIENCY_HAND_COUNT;
    extern const int VIMFICIENCY_COUNT_CLASS_COUNT;

    typedef struct {
        double keyWeight;
        double sameFingerWeight;
        double sameKeyWeight;
        double altHandWeight;
        double goodRollWeight;
        double badRollWeight;
    } C_ScoreWeights;

    typedef struct {
        int8_t hand;
        int8_t finger;
        double base_cost;
    } C_KeyInfo;

    typedef struct {
        bool has_base;
        double base;
        bool has_count_slope;
        double count_slope;
        bool has_span_slope;
        double span_slope;
    } C_CountPenaltyOverride;

    typedef struct {
        int default_keyboard;
        C_ScoreWeights weights;
        C_KeyInfo keys[61];  // must match C++ KEY_COUNT
        int slice_buffer_amount;
        int32_t shiftwidth;
        bool use_count_penalty_overrides;
        C_CountPenaltyOverride count_penalty_overrides[14];  // must match C++ CountClassCOUNT
    } VimficiencyConfigFFI;

    VimficiencyConfigFFI* vimficiency_get_config();
    void vimficiency_apply_config();

    const char* vimficiency_key_name(int index);
    const char* vimficiency_finger_name(int index);
    const char* vimficiency_hand_name(int index);
    const char* vimficiency_count_class_name(int index);
    const char* vimficiency_analyze(
        const char* initial_text,
        const char* goal_text,
        int boundary_first_col, int boundary_last_col,
        bool has_lines_above, bool has_lines_below,
        int start_row, int start_col, int end_row, int end_col,
        const char* keyseq,
        int window_height, int scroll_amount,
        int RESULTS_CALCULATED
    );
    const char* vimficiency_get_debug();

    int vimficiency_version();

    const char* vimficiency_debug_config();

    const char* vimficiency_tokenize_motions(const char* seq);

    const char* vimficiency_tokenize_sequence(const char* seq);

    const char* vimficiency_build_sequence(const char* encoded_events);

    const char* vimficiency_compute_search_region(
        const char* encoded_start_lines,
        const char* encoded_end_lines,
        int start_row,
        int end_row,
        int padding
    );

    int vimficiency_resolve_recall_cutoff(
        const char* encoded_records,
        int64_t target_hrtime,
        int budget
    );

    int vimficiency_manual_evict_reason(
        int start_row,
        int cursor_row,
        int64_t last_key_time_ns,
        bool has_last_key,
        int64_t now_ns,
        int max_search_lines,
        int manual_idle_timeout_seconds
    );

    const char* vimficiency_format_sequence(const char* seq);
]])

local function find_plugin_root()
	local source = debug.getinfo(1, "S").source
	if source:sub(1, 1) == "@" then
		source = source:sub(2) -- remove leading @
	end
	return vim.fn.fnamemodify(source, ":h:h:h")
end

--- Find and load the shared library.
--- `_G.__vimficiency_reload_lib_path` lets `:Vimfy reload` bypass dlopen caching.
---@return VimficiencyLib
local function load_lib()
	local root = find_plugin_root()
	local paths = {}
	if type(_G.__vimficiency_reload_lib_path) == "string" then
		table.insert(paths, _G.__vimficiency_reload_lib_path)
	end
	table.insert(paths, root .. "/build/libvimficiency.so") -- local build
	table.insert(paths, "vimficiency") -- system path

	for _, path in ipairs(paths) do
		local ok, lib = pcall(ffi.load, path)
		if ok then
			return lib
		end
	end

	error("Could not load libvimficiency.so - tried: " .. table.concat(paths, ", "))
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

---@type VimficiencyLib
local lib = load_lib()

M.Key = build_enum(lib.VIMFICIENCY_KEY_COUNT, lib.vimficiency_key_name)
M.Finger = build_enum(lib.VIMFICIENCY_FINGER_COUNT, lib.vimficiency_finger_name)
M.Hand = build_enum(lib.VIMFICIENCY_HAND_COUNT, lib.vimficiency_hand_name)
M.CountClass = build_enum(lib.VIMFICIENCY_COUNT_CLASS_COUNT, lib.vimficiency_count_class_name)

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

---@param key_seq VimficiencyKeyEvent[]
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
	local result = ffi.string(lib.vimficiency_build_sequence(table.concat(parts)))
	if result:sub(1, 7) == "ERROR: " then
		error("vimficiency_build_sequence failed: " .. result, 0)
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
	local result = ffi.string(lib.vimficiency_compute_search_region(
		encode_string_list(start_lines),
		encode_string_list(end_lines),
		start_row,
		end_row,
		padding
	))
	if result:sub(1, 7) == "ERROR: " then
		error("vimficiency_compute_search_region failed: " .. result, 0)
	end
	local parts = vim.split(result, EVENT_FIELD_SEP, { plain = true, trimempty = true })
	assert(#parts == 2, "vimficiency_compute_search_region returned malformed payload")
	return tonumber(parts[1]), tonumber(parts[2])
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
		parts[#parts + 1] = tostring(rec.time_started)
		parts[#parts + 1] = rec.first_mode or ""
	end
	local index = lib.vimficiency_resolve_recall_cutoff(
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
	local code = lib.vimficiency_manual_evict_reason(
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

-- The (has_<name>, <name>) flag pairs on C_CountPenaltyOverride. Genuine
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
				error(string.format("vimficiency: invalid value for '%s%s': %s",
					key_prefix or "", tostring(k), tostring(err)))
			elseif strict and status == "unknown" then
				error(string.format("vimficiency: unknown config key '%s%s'",
					key_prefix or "", tostring(k)))
			end
		end
	end
	return consumed
end

-- Allowed keys on a single count_penalty_overrides[class] entry. Mirrors
-- OVERRIDE_FIELDS; a set form lets us validate keys in O(1).
local OVERRIDE_FIELD_SET = { base = true, count_slope = true, span_slope = true }

-- ---@param user_config VimficiencyConfigFFI
---@return table<string, true> consumed  Top-level user_config keys claimed by C++ side
function M.configure(user_config)
	---@type VimficiencyConfigFFI
	local config = lib.vimficiency_get_config()

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
		for i = 0, lib.VIMFICIENCY_COUNT_CLASS_COUNT - 1 do
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
			if class_index < 0 or class_index >= lib.VIMFICIENCY_COUNT_CLASS_COUNT then
				error("Count penalty class out of range: " .. tostring(class_key))
			end

			for field in pairs(override) do
				if not OVERRIDE_FIELD_SET[field] then
					error(string.format(
						"vimficiency: unknown count_penalty_overrides[%s] key '%s' (allowed: base, count_slope, span_slope)",
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

	lib.vimficiency_apply_config()
	return consumed
end

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
---@param RESULTS_CALCULATED integer
---@return VimficiencyResult[] results, number user_cost, string debug
function M.analyze(
  initial_lines, goal_lines,
  boundary_first_col, boundary_last_col,
  has_lines_above, has_lines_below,
  start_row, start_col, end_row, end_col,
  key_seq,
  window_height, scroll_amount,
  RESULTS_CALCULATED
)
	local initial_text = table.concat(initial_lines, "\n")
	local goal_text = table.concat(goal_lines, "\n")

	local result = lib.vimficiency_analyze(
    initial_text, goal_text,
    boundary_first_col, boundary_last_col,
    has_lines_above, has_lines_below,
    start_row, start_col, end_row, end_col,
    key_seq,
    window_height, scroll_amount,
    RESULTS_CALCULATED
  )
  local dbg = ffi.string(lib.vimficiency_get_debug())
  local result_str = ffi.string(result)

  -- Check for error from C++
  if result_str:sub(1, 6) == "ERROR:" then
    error(result_str)
  end

  -- Parse results: format is "size: N user_cost: X.XXX\nseq1 cost1\nseq2 cost2\n..."
  ---@class VimficiencyResult
  ---@field seq string Motion sequence
  ---@field cost number Effort cost

  ---@type VimficiencyResult[]
  local results = {}
  local user_cost = 0
  local line_num = 0
  for line in result_str:gmatch("[^\n]+") do
    -- Stop parsing at debug separator
    if line:find("----------------DEBUG----------------", 1, true) then
      break
    end
    line_num = line_num + 1
    if line_num == 1 then
      -- Parse header: "size: N user_cost: X.XXX"
      local cost_str = line:match("user_cost:%s*(%S+)")
      if cost_str then
        user_cost = tonumber(cost_str) or 0
      end
    else
      local seq, cost_str = line:match("^(%S+)%s+(%S+)")
      if seq then
        table.insert(results, {
          seq = seq,
          cost = tonumber(cost_str) or 0
        })
      end
    end
  end

  return results, user_cost, dbg
end

function M.version()
	return lib.vimficiency_version()
end

function M.debug_config()
	return ffi.string(lib.vimficiency_debug_config())
end

--- Tokenize a motion sequence into individual tokens
---@param seq string Motion sequence (e.g., "3wfx;j")
---@return string[] Array of motion tokens (e.g., {"3w", "fx;", "j"})
---@return string|nil error Error message if tokenization failed
function M.tokenize_motions(seq)
  if not seq or seq == "" then
    return {}, nil
  end
  local result_str = ffi.string(lib.vimficiency_tokenize_motions(seq))
  if result_str == "" then
    return {}, nil
  end
  -- Check for error from C++
  if result_str:sub(1, 6) == "ERROR:" then
    return {}, result_str
  end
  -- Handle trailing newline from C++ (trimws removes empty strings from split)
  return vim.split(result_str, "\n", { plain = true, trimempty = true }), nil
end

--- Tokenize a full Vim sequence (motions, edits, insert-mode text) into tokens
--- Supports change commands, typed text, and <Esc>
---@param seq string Vim sequence (e.g., "ciwhello<Esc>2j")
---@return string[] Array of tokens (e.g., {"ciw", "hello", "<Esc>", "2j"})
---@return string|nil error Error message if tokenization failed
function M.tokenize_sequence(seq)
  if not seq or seq == "" then
    return {}, nil
  end
  local result_str = ffi.string(lib.vimficiency_tokenize_sequence(seq))
  if result_str == "" then
    return {}, nil
  end
  -- Check for error from C++
  if result_str:sub(1, 6) == "ERROR:" then
    return {}, result_str
  end
  -- Handle trailing newline from C++ (trimws removes empty strings from split)
  return vim.split(result_str, "\n", { plain = true, trimempty = true }), nil
end

--- Format a sequence string for human-readable display
--- Tokenizes into logical units and joins with spaces
---@param seq string Vim sequence (e.g., "3rx<C-d>ciwfoo<Esc>")
---@return string Formatted sequence (e.g., "3rx <C-d> ciw foo <Esc>")
function M.format_sequence(seq)
  if not seq or seq == "" then
    return ""
  end
  return ffi.string(lib.vimficiency_format_sequence(seq))
end

return M
