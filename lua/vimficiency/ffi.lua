-- lua/vimficiency/ffi.lua

local ffi = require("ffi")

local M = {}

--------------------------------------------------------------------------------
-- FFI Type Annotations (for LuaLS)
-- These mirror the C structs defined in ffi.cdef below.
--------------------------------------------------------------------------------

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

    const char* vimficiency_format_sequence(const char* seq);
]])

-------- Local Helper Functions --------

local function find_plugin_root()
	-- This file is at: <plugin_root>/lua/vimficiency/ffi.lua,
	local source = debug.getinfo(1, "S").source
	if source:sub(1, 1) == "@" then
		source = source:sub(2) -- remove leading @
	end
	return vim.fn.fnamemodify(source, ":h:h:h")
end

-- Find and load the shared library
---@return VimficiencyLib
local function load_lib()
	local root = find_plugin_root()
	local paths = {
		root .. "/build/libvimficiency.so", -- local build
		"vimficiency", -- system path
	}

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

-------- END Local Helper Functions --------

---@type VimficiencyLib
local lib = load_lib()

M.Key = build_enum(lib.VIMFICIENCY_KEY_COUNT, lib.vimficiency_key_name)
M.Finger = build_enum(lib.VIMFICIENCY_FINGER_COUNT, lib.vimficiency_finger_name)
M.Hand = build_enum(lib.VIMFICIENCY_HAND_COUNT, lib.vimficiency_hand_name)
M.CountClass = build_enum(lib.VIMFICIENCY_COUNT_CLASS_COUNT, lib.vimficiency_count_class_name)

-- ---@param user_config VimficiencyConfigFFI
function M.configure(user_config)
	---@type VimficiencyConfigFFI
	local config = lib.vimficiency_get_config()

  if user_config.default_keyboard then
    config.default_keyboard = user_config.default_keyboard
  end

	if user_config.weights then
		local w = user_config.weights
		local cw = config.weights
		if w.keyWeight then
			cw.keyWeight = w.keyWeight
		end
		if w.sameFingerWeight then
			cw.sameFingerWeight = w.sameFingerWeight
		end
		if w.sameKeyWeight then
			cw.sameKeyWeight = w.sameKeyWeight
		end
		if w.altHandWeight then
			cw.altHandWeight = w.altHandWeight
		end
		if w.goodRollWeight then
			cw.goodRollWeight = w.goodRollWeight
		end
		if w.badRollWeight then
			cw.badRollWeight = w.badRollWeight
		end
	end

	if user_config.keys then
		for key_index, info in pairs(user_config.keys) do
			config.keys[key_index].hand = info.hand
			config.keys[key_index].finger = info.finger
			config.keys[key_index].base_cost = info.cost
		end
	end

  if user_config.slice_buffer_amount then
    config.slice_buffer_amount = user_config.slice_buffer_amount
  end

  if user_config.shiftwidth then
    config.shiftwidth = user_config.shiftwidth
  end

  if user_config.use_count_penalty_overrides ~= nil then
    config.use_count_penalty_overrides = user_config.use_count_penalty_overrides
  end

  if user_config.count_penalty_overrides then
    if user_config.use_count_penalty_overrides == nil then
      config.use_count_penalty_overrides = true
    end

    for i = 0, lib.VIMFICIENCY_COUNT_CLASS_COUNT - 1 do
      local dst = config.count_penalty_overrides[i]
      dst.has_base = false
      dst.has_count_slope = false
      dst.has_span_slope = false
    end

    for class_key, override in pairs(user_config.count_penalty_overrides) do
      local class_index = nil
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

      local dst = config.count_penalty_overrides[class_index]
      if override.base ~= nil then
        dst.has_base = true
        dst.base = override.base
      end
      if override.count_slope ~= nil then
        dst.has_count_slope = true
        dst.count_slope = override.count_slope
      end
      if override.span_slope ~= nil then
        dst.has_span_slope = true
        dst.span_slope = override.span_slope
      end
    end
  end

	lib.vimficiency_apply_config()
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
