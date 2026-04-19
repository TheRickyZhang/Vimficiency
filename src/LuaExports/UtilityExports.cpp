#include "LuaExports/Shared.h"
#include "Interpreter/MotionInterpreter.h"
#include "Interpreter/SequenceFormatting.h"
#include "Interpreter/SequenceParser.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>

using namespace std;

namespace vimficiency::lua_exports::payload {

Result<vector<string>> decodeLengthPrefixedStrings(string_view encoded) {
  vector<string> parts;
  size_t pos = 0;
  while (pos < encoded.size()) {
    const size_t colon = encoded.find(':', pos);
    if (colon == string_view::npos) {
      return export_helpers::unexpectedError(
          ExportErrorKind::InvalidPayload,
          "invalid length-prefixed payload");
    }
    const string_view lenText = encoded.substr(pos, colon - pos);
    if (lenText.empty()) {
      return export_helpers::unexpectedError(
          ExportErrorKind::InvalidPayload,
          "invalid length-prefixed payload");
    }
    size_t len = 0;
    for (char c : lenText) {
      if (c < '0' || c > '9') {
        return export_helpers::unexpectedError(
            ExportErrorKind::InvalidPayload,
            "invalid length-prefixed payload");
      }
      const size_t digit = static_cast<size_t>(c - '0');
      if (len > (numeric_limits<size_t>::max() - digit) / 10) {
        return export_helpers::unexpectedError(
            ExportErrorKind::PayloadTooLarge,
            "length-prefixed payload too large");
      }
      len = len * 10 + digit;
    }
    pos = colon + 1;
    if (pos + len > encoded.size()) {
      return export_helpers::unexpectedError(
          ExportErrorKind::TruncatedPayload,
          "truncated length-prefixed payload");
    }
    parts.emplace_back(encoded.substr(pos, len));
    pos += len;
  }
  return parts;
}

Result<Lines> decodeLineArray(string_view encoded) {
  return decodeLengthPrefixedStrings(encoded).transform([](const vector<string>& parts) {
    Lines lines;
    lines.reserve(parts.size());
    for (const auto& part : parts) {
      lines.push_back(part);
    }
    return lines;
  });
}

Result<vector<RecallRecordMeta>> decodeRecallRecordMeta(string_view encoded) {
  return decodeLengthPrefixedStrings(encoded).and_then([](const vector<string>& parts)
      -> Result<vector<RecallRecordMeta>> {
    if (parts.size() % 2 != 0) {
      return export_helpers::unexpectedError(
          ExportErrorKind::InvalidPayload,
          "invalid recall metadata payload");
    }

    vector<RecallRecordMeta> records;
    records.reserve(parts.size() / 2);
    for (size_t i = 0; i < parts.size(); i += 2) {
      int64_t parsed = 0;
      const char* begin = parts[i].data();
      const char* end = begin + parts[i].size();
      const auto [ptr, ec] = from_chars(begin, end, parsed);
      if (ec != errc{} || ptr != end) {
        return export_helpers::unexpectedError(
            ExportErrorKind::InvalidValue,
            "invalid recall timestamp");
      }
      records.push_back({ parsed, parts[i + 1] });
    }
    return records;
  });
}

Result<vector<KeyTrackingEvent>> decodeKeyTrackingEvents(string_view encoded) {
  vector<KeyTrackingEvent> events;
  size_t start = 0;
  while (start < encoded.size()) {
    size_t end = encoded.find(kEventRecordSep, start);
    if (end == string_view::npos) {
      end = encoded.size();
    }
    const string_view record = encoded.substr(start, end - start);
    if (!record.empty()) {
      const size_t sep = record.find(kEventFieldSep);
      if (sep == string_view::npos) {
        return export_helpers::unexpectedError(
            ExportErrorKind::InvalidPayload,
            "invalid key event payload");
      }
      events.push_back({
          string(record.substr(0, sep)),
          string(record.substr(sep + 1)),
      });
    }
    start = end + 1;
  }
  return events;
}

}  // namespace vimficiency::lua_exports::payload

namespace vimficiency::lua_exports::pure_logic {

namespace {

bool isCleanBoundaryMode(string_view mode) {
  if (mode.empty()) return false;
  if (mode.substr(0, min<size_t>(2, mode.size())) == "no") return false;
  return mode.front() == 'n';
}

pair<int, int> findDiffRange(const Lines& a, const Lines& b) {
  int firstDiff = -1;
  int lastDiff = -1;
  const size_t maxLen = max(a.size(), b.size());
  for (size_t i = 0; i < maxLen; ++i) {
    const string* lhs = i < a.size() ? &a[i] : nullptr;
    const string* rhs = i < b.size() ? &b[i] : nullptr;
    if (lhs == rhs) continue;
    if (!lhs || !rhs || *lhs != *rhs) {
      if (firstDiff < 0) firstDiff = static_cast<int>(i);
      lastDiff = static_cast<int>(i);
    }
  }
  return {firstDiff, lastDiff};
}

}  // namespace

Result<string> buildKeySequence(string_view encoded) {
  return payload::decodeKeyTrackingEvents(encoded).transform([](const vector<payload::KeyTrackingEvent>& events) {
    string out;
    out.reserve(events.size() * 2);

    size_t i = 0;
    while (i < events.size()) {
      const auto& curr = events[i];
      bool dominated = false;
      if (i + 1 < events.size()) {
        const auto& next = events[i + 1];
        const bool sameKey = curr.keyTyped == next.keyTyped;
        const string currMode = curr.mode.substr(0, min<size_t>(2, curr.mode.size()));
        const string nextMode = next.mode.substr(0, min<size_t>(2, next.mode.size()));
        if (sameKey && currMode == "no" && nextMode != "no") {
          out += curr.keyTyped;
          i += 2;
          dominated = true;
        }
      }
      if (!dominated) {
        out += curr.keyTyped;
        i += 1;
      }
    }

    return out;
  });
}

pair<int, int> computeSearchRegion(
    int startRow,
    int endRow,
    const Lines& startLines,
    const Lines& endLines,
    int padding) {
  int minRow = min(startRow, endRow);
  int maxRow = max(startRow, endRow);
  const auto [firstDiff, lastDiff] = findDiffRange(startLines, endLines);
  if (firstDiff >= 0) {
    minRow = min(minRow, firstDiff);
    maxRow = max(maxRow, lastDiff);
  }

  const int bufferLen = static_cast<int>(max(startLines.size(), endLines.size()));
  const int regionStart = max(0, minRow - padding);
  const int regionEnd = min(bufferLen - 1, maxRow + padding);
  return {regionStart, regionEnd};
}

int resolveRecallCutoffIndex(
    const vector<payload::RecallRecordMeta>& records,
    int64_t targetHrtime,
    int budget) {
  int lo = 0;
  int hi = static_cast<int>(records.size()) - 1;
  int best = -1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (records[mid].timeStarted <= targetHrtime) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (best < 0) return 0;

  int steps = 0;
  for (int i = best; i >= 0 && steps <= budget; --i, ++steps) {
    if (isCleanBoundaryMode(records[i].firstMode)) {
      return i + 1;
    }
  }
  return 0;
}

int manualEvictReason(
    int startRow,
    int cursorRow,
    int64_t lastKeyTimeNs,
    bool hasLastKey,
    int64_t nowNs,
    int maxSearchLines,
    int manualIdleTimeoutSeconds) {
  if (abs(cursorRow - startRow) + 1 > maxSearchLines) {
    return kManualEvictDrift;
  }
  if (hasLastKey && nowNs - lastKeyTimeNs > static_cast<int64_t>(manualIdleTimeoutSeconds) * 1000000000LL) {
    return kManualEvictIdle;
  }
  return kManualEvictNone;
}

}  // namespace vimficiency::lua_exports::pure_logic

extern "C" {

// Wire format for tokenize_* FFI: one token per line, `<kind>\t<text>\n`.
// `<kind>` is a single ASCII char encoding the `TokenType` — see switch
// below. `<text>` is the raw token as emitted by the parser. Token text
// never contains `\t` or `\n` (special keys are stored as literal
// `<Key>` strings), so these separators are unambiguous.
//
// The kind payload is the whole point of this format: without it, the
// Lua side would keep parallel classifier tables to re-derive what the
// parser already knows, and they'd drift. See `dev/lua/replay-precompute.md`.
static char tokenKindChar(TokenType type) {
  switch (type) {
    case TokenType::Motion:    return 'M';
    case TokenType::Delete:    return 'D';
    case TokenType::Change:    return 'C';
    case TokenType::Visual:    return 'V';
    case TokenType::TypedText: return 'T';
    case TokenType::Escape:    return 'E';
  }
  return '?';  // Unreachable — compiler should warn if a case is missed.
}

const char *vimficiency_tokenize_motions(const char *seq) {
  static string result_storage;
  return vimficiency::lua_exports::export_helpers::storeOptionalString(result_storage, seq, [](string_view input) {
    string owned(input);
    return parseMotions(owned)
        .transform([](const vector<ParsedMotion>& motions) {
          // All motion-parser output is TokenType::Motion by construction.
          vector<string> rows;
          rows.reserve(motions.size());
          for (const auto& motion : motions) {
            ostringstream oss;
            oss << motion;
            rows.push_back(string{tokenKindChar(TokenType::Motion)} + "\t" + oss.str());
          }
          return vimficiency::lua_exports::export_helpers::joinWithTrailingNewline(rows);
        })
        .transform_error([](const MotionParseError& error) {
          return vimficiency::lua_exports::ExportError{
              .kind = vimficiency::lua_exports::ExportErrorKind::InvalidValue,
              .message = formatMotionParseError(error),
          };
        });
  });
}

const char *vimficiency_tokenize_sequence(const char *seq) {
  static string result_storage;
  return vimficiency::lua_exports::export_helpers::storeOptionalString(result_storage, seq, [](string_view input) {
    return parseSequence(input)
        .transform([](const vector<SequenceToken>& tokens) {
          vector<string> rows;
          rows.reserve(tokens.size());
          for (const auto& tok : tokens) {
            rows.push_back(string{tokenKindChar(tok.type)} + "\t" + tok.text);
          }
          return vimficiency::lua_exports::export_helpers::joinWithTrailingNewline(rows);
        })
        .transform_error([](const SequenceParseError& error) {
          return vimficiency::lua_exports::ExportError{
              .kind = vimficiency::lua_exports::ExportErrorKind::InvalidValue,
              .message = formatSequenceParseError(error),
          };
        });
  });
}

const char *vimficiency_build_sequence(const char *encoded_events) {
  static string result_storage;
  return vimficiency::lua_exports::export_helpers::storeOptionalString(result_storage, encoded_events, [](string_view input) {
    return vimficiency::lua_exports::pure_logic::buildKeySequence(input);
  });
}

const char *vimficiency_compute_search_region(
    const char *encoded_start_lines,
    const char *encoded_end_lines,
    int start_row,
    int end_row,
    int padding) {
  static string result_storage;
  return vimficiency::lua_exports::export_helpers::storeString(result_storage, [&] {
    return vimficiency::lua_exports::export_helpers::requiredText(encoded_start_lines, "encoded_start_lines")
        .and_then(vimficiency::lua_exports::payload::decodeLineArray)
        .and_then([&](const Lines& startLines) {
          return vimficiency::lua_exports::export_helpers::requiredText(encoded_end_lines, "encoded_end_lines")
              .and_then(vimficiency::lua_exports::payload::decodeLineArray)
              .transform([&](const Lines& endLines) {
                const auto [regionStart, regionEnd] =
                    vimficiency::lua_exports::pure_logic::computeSearchRegion(
                        start_row, end_row, startLines, endLines, padding);
                return vimficiency::lua_exports::export_helpers::packInts(regionStart, regionEnd);
              });
        });
  });
}

int vimficiency_resolve_recall_cutoff(
    const char *encoded_records,
    int64_t target_hrtime,
    int budget) {
  return vimficiency::lua_exports::export_helpers::storeIntOr(0, [&] {
    return vimficiency::lua_exports::payload::decodeRecallRecordMeta(
        vimficiency::lua_exports::export_helpers::optionalText(encoded_records))
        .transform([&](const vector<vimficiency::lua_exports::payload::RecallRecordMeta>& records) {
          return vimficiency::lua_exports::pure_logic::resolveRecallCutoffIndex(
              records,
              target_hrtime,
              budget);
        });
  });
}

int vimficiency_manual_evict_reason(
    int start_row,
    int cursor_row,
    int64_t last_key_time_ns,
    bool has_last_key,
    int64_t now_ns,
    int max_search_lines,
    int manual_idle_timeout_seconds) {
  return vimficiency::lua_exports::pure_logic::manualEvictReason(
      start_row,
      cursor_row,
      last_key_time_ns,
      has_last_key,
      now_ns,
      max_search_lines,
      manual_idle_timeout_seconds);
}

const char *vimficiency_format_sequence(const char *seq) {
  static string result_storage;
  return vimficiency::lua_exports::export_helpers::storeOptionalString(result_storage, seq, [](string_view input) {
    return vimficiency::lua_exports::Result<string>(
        formatSequenceForDisplay(string(input)));
  });
}

}
