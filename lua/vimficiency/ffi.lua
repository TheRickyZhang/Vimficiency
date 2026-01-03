-- lua/vimficiency/ffi.lua

local ffi = require("ffi")

local M = {}

--------------------------------------------------------------------------------
-- FFI Type Annotations (for LuaLS)
-- These mirror the C structs defined in ffi.cdef below.
--------------------------------------------------------------------------------

---@class C_ScoreWeights
---@field w_key number
---@field w_same_finger number
---@field w_same_key number
---@field w_alt_bonus number
---@field w_run_pen number
---@field w_roll_good number
---@field w_roll_bad number

---@class C_KeyInfo
---@field hand integer
---@field finger integer
---@field base_cost number

---@class VimficiencyConfigFFI
---@field default_keyboard integer
---@field weights C_ScoreWeights
---@field keys C_KeyInfo[]
---@field slice_buffer_count integer

---@class VimficiencyLib
---@field VIMFICIENCY_KEY_COUNT integer
---@field VIMFICIENCY_FINGER_COUNT integer
---@field VIMFICIENCY_HAND_COUNT integer
---@field vimficiency_key_name fun(index: integer): ffi.cdata*
---@field vimficiency_finger_name fun(index: integer): ffi.cdata*
---@field vimficiency_hand_name fun(index: integer): ffi.cdata*
---@field vimficiency_get_config fun(): VimficiencyConfigFFI
---@field vimficiency_apply_config fun(): nil
---@field vimficiency_analyze fun(text: string, includes_real_top: boolean, includes_real_bottom: boolean, start_row: integer, start_col: integer, end_row: integer, end_col: integer, keyseq: string, top_row: integer, bottom_row: integer, window_height: integer, scroll_amount: integer, results_calculated: integer): string
---@field vimficiency_get_debug fun(): string
---@field vimficiency_version fun(): integer
---@field vimficiency_debug_config fun(): string
---@field vimficiency_tokenize_motions fun(seq: string): string

ffi.cdef([[
    extern const int VIMFICIENCY_KEY_COUNT;
    extern const int VIMFICIENCY_FINGER_COUNT;
    extern const int VIMFICIENCY_HAND_COUNT;

    typedef struct {
        double w_key;
        double w_same_finger;
        double w_same_key;
        double w_alt_bonus;
        double w_run_pen;
        double w_roll_good;
        double w_roll_bad;
    } C_ScoreWeights;

    typedef struct {
        int8_t hand;
        int8_t finger;
        double base_cost;
    } C_KeyInfo;

    typedef struct {
        int default_keyboard;
        C_ScoreWeights weights;
        C_KeyInfo keys[64];  // assert(64 >= key_count)
        int slice_buffer_amount;
    } VimficiencyConfigFFI;

    VimficiencyConfigFFI* vimficiency_get_config();
    void vimficiency_apply_config();

    const char* vimficiency_key_name(int index);
    const char* vimficiency_finger_name(int index);
    const char* vimficiency_hand_name(int index);
    const char* vimficiency_analyze(
        const char* text, bool includes_real_top, bool includes_real_bottom,
        int start_row, int start_col, int end_row, int end_col,
        const char* keyseq,
        int top_row, int bottom_row, int window_height, int scroll_amount,
        int RESULTS_CALCULATED
    );
    const char* vimficiency_get_debug();

    int vimficiency_version();

    const char* vimficiency_debug_config();

    const char* vimficiency_tokenize_motions(const char* seq);
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
		if w.w_key then
			cw.w_key = w.w_key
		end
		if w.w_same_finger then
			cw.w_same_finger = w.w_same_finger
		end
		if w.w_same_key then
			cw.w_same_key = w.w_same_key
		end
		if w.w_alt_bonus then
			cw.w_alt_bonus = w.w_alt_bonus
		end
		if w.w_run_pen then
			cw.w_run_pen = w.w_run_pen
		end
		if w.w_roll_good then
			cw.w_roll_good = w.w_roll_good
		end
		if w.w_roll_bad then
			cw.w_roll_bad = w.w_roll_bad
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
    config.slice_buffer_count = user_config.slice_buffer_amount
  end

	lib.vimficiency_apply_config()
end

---@param lines string[]
---@param includes_real_top boolean
---@param includes_real_bottom boolean
---@param start_row integer (0-indexed)
---@param start_col integer (0-indexed)
---@param end_row integer (0-indexed)
---@param end_col integer (0-indexed)
---@param key_seq string
---@param top_row integer
---@param bottom_row integer
---@param window_height integer
---@param scroll_amount integer
---@param RESULTS_CALCULATED integer
---@return VimficiencyResult[] results, string debug
function M.analyze(
  lines, includes_real_top, includes_real_bottom,
  start_row, start_col, end_row, end_col,
  key_seq,
  top_row, bottom_row, window_height, scroll_amount,
  RESULTS_CALCULATED
)
	local text = table.concat(lines, "\n")

	local result = lib.vimficiency_analyze(
    text, includes_real_top, includes_real_bottom,
    start_row, start_col, end_row, end_col,
    key_seq,
    top_row, bottom_row, window_height, scroll_amount,
    RESULTS_CALCULATED
  )
  local dbg = ffi.string(lib.vimficiency_get_debug())
  local result_str = ffi.string(result)

  -- Check for error from C++
  if result_str:sub(1, 6) == "ERROR:" then
    error(result_str)
  end

  -- Parse results: format is "size: N\nseq1 cost1\nseq2 cost2\n..."
  ---@class VimficiencyResult
  ---@field seq string Motion sequence
  ---@field cost number Effort cost

  ---@type VimficiencyResult[]
  local results = {}
  local line_num = 0
  for line in result_str:gmatch("[^\n]+") do
    line_num = line_num + 1
    if line_num > 1 then  -- Skip "size: N" header
      local seq, cost_str = line:match("^(%S+)%s+(%S+)")
      if seq then
        table.insert(results, {
          seq = seq,
          cost = tonumber(cost_str) or 0
        })
      end
    end
  end

  return results, dbg
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

return M
