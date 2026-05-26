local config = require("vimficiency.config")
local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local function checked_string(slice)
  local text = core.slice_to_string(slice)
  if text:sub(1, 6) == "ERROR:" then
    error(text, 2)
  end
  return text
end

local function resolved_opts(opts)
  local defaults = config.sequence_display or {}
  opts = opts or {}
  local tokenize = defaults.tokenize ~= false
  local sectionize = defaults.sectionize ~= false
  if opts.tokenize ~= nil then
    tokenize = opts.tokenize
  end
  if opts.sectionize ~= nil then
    sectionize = opts.sectionize
  end
  return {
    tokenize = tokenize,
    sectionize = sectionize,
  }
end

local function required_number(result, key)
  local value = result[key]
  assert(type(value) == "number", "result." .. key .. " must be a number")
  return tostring(value)
end

local function required_integer(result, key)
  local value = result[key]
  assert(type(value) == "number", "result." .. key .. " must be a number")
  return value
end

local function encode_result(result)
  result = result or {}
  local recs = {}
  for _, rec in ipairs(result.optimal_results or {}) do
    recs[#recs + 1] = rec.seq or ""
    recs[#recs + 1] = tostring(rec.cost or 0)
  end

  local has_user_cost = type(result.user_cost) == "number"
  return core.encode_string_list({
    core.encode_string_list(result.lines or {}),
    required_number(result, "start_row"),
    required_number(result, "start_col"),
    required_number(result, "end_row"),
    required_number(result, "end_col"),
    result.user_seq or "",
    has_user_cost and "1" or "0",
    has_user_cost and tostring(result.user_cost) or "",
    result.finish_reason or "",
    core.encode_string_list(recs),
  })
end

local function encode_body_result(result)
  result = result or {}
  return encode_result({
    lines = result.lines or {},
    start_row = result.start_row or 0,
    start_col = result.start_col or 0,
    end_row = result.end_row or 0,
    end_col = result.end_col or 0,
    user_seq = result.user_seq,
    user_cost = result.user_cost,
    finish_reason = result.finish_reason,
    optimal_results = result.optimal_results,
  })
end

function M.sequence_display_lines(seq, opts)
  local resolved = resolved_opts(opts)
  local payload = checked_string(lib.vf_sequence_display_lines(
    seq or "",
    #(seq or ""),
    resolved.tokenize,
    resolved.sectionize
  ))
  return core.decode_string_list(payload)
end

function M.sequence_display_inline(seq, opts)
  local resolved = resolved_opts(opts)
  return checked_string(lib.vf_sequence_display_inline(
    seq or "",
    #(seq or ""),
    resolved.tokenize
  ))
end

function M.sequence_display_key_notation(text)
  text = text or ""
  return checked_string(lib.vf_sequence_display_key_notation(text, #text))
end

function M.sequence_display_typed_text(text, key_notation)
  text = text or ""
  return checked_string(lib.vf_sequence_display_typed_text(
    text,
    #text,
    key_notation
  ))
end

function M.sequence_display_typed_chunks(chunks, opts)
  local resolved = resolved_opts(opts)
  local fields = {}
  for _, chunk in ipairs(chunks or {}) do
    fields[#fields + 1] = chunk.kind or ""
    fields[#fields + 1] = chunk.text or ""
  end
  local payload = core.encode_string_list(fields)
  return checked_string(lib.vf_sequence_display_typed_chunks(
    payload,
    #payload,
    resolved.tokenize
  ))
end

function M.format_result_position(result)
  result = result or {}
  return checked_string(lib.vf_format_result_position(
    required_integer(result, "start_row"),
    required_integer(result, "start_col"),
    required_integer(result, "end_row"),
    required_integer(result, "end_col")
  ))
end

function M.format_result_reason_suffix(result)
  local reason = result and result.finish_reason or ""
  return checked_string(lib.vf_format_result_reason_suffix(reason, #reason))
end

function M.format_result_body(result, opts)
  local resolved = resolved_opts(opts)
  local payload = encode_body_result(result)
  local encoded_lines = checked_string(lib.vf_format_result_body(
    payload,
    #payload,
    resolved.tokenize,
    resolved.sectionize
  ))
  return core.decode_string_list(encoded_lines)
end

function M.format_result_message(title, result, opts)
  local resolved = resolved_opts(opts)
  local payload = encode_result(result)
  return checked_string(lib.vf_format_result_message(
    title or "",
    #(title or ""),
    payload,
    #payload,
    resolved.tokenize,
    resolved.sectionize
  ))
end

function M.format_saved_result_lines(name, result, opts)
  local resolved = resolved_opts(opts)
  local payload = encode_result(result)
  local encoded_lines = checked_string(lib.vf_format_saved_result_lines(
    name or "",
    #(name or ""),
    payload,
    #payload,
    resolved.tokenize,
    resolved.sectionize
  ))
  return core.decode_string_list(encoded_lines)
end

return M
