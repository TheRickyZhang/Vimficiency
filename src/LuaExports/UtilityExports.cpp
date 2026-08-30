#include "LuaExports/Common.h"
#include "Interpreter/MovementInterpreter.h"
#include "Interpreter/SequenceFormatting.h"
#include "Interpreter/SequenceParser.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Optimizer/OptimizerParamsBase.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Utils/Debug.h"

#include <sstream>

using namespace std;
namespace helpers = VF::LuaExports::helpers;
namespace payload = VF::LuaExports::payload;
namespace logic = VF::LuaExports::logic;
using VF::LuaExports::ExportError;
using VF::LuaExports::ExportErrorKind;
using VF::LuaExports::Result;

namespace {

// Wire format for tokenize_* FFI: one token per line, `<kind>\t<text>\n`.
// `<kind>` is a single ASCII char encoding the `TokenKind` — see switch
// below. `<text>` is the raw token as emitted by the parser. Token text
// never contains `\t` or `\n` (special keys are stored as literal
// `<Key>` strings), so these separators are unambiguous.
//
// The kind payload is the whole point of this format: without it, the
// Lua side would keep parallel classifier tables to re-derive what the
// parser already knows, and they'd drift. See `dev/lua/replay-precompute.md`.
char tokenKindChar(TokenKind kind) {
  switch (kind) {
    case TokenKind::Movement:    return 'M';
    case TokenKind::Delete:    return 'D';
    case TokenKind::Change:    return 'C';
    case TokenKind::Visual:    return 'V';
    case TokenKind::TypedText: return 'T';
    case TokenKind::Escape:    return 'E';
  }
  return '?';  // Unreachable — compiler should warn if a case is missed.
}

}  // namespace

extern "C" {

VFByteSlice vf_tokenize_movements(const char *seq, size_t seq_len) {
  static string result_storage;
  return helpers::storeBytes(result_storage,
      helpers::requiredBytes(seq, seq_len, "seq")
          .and_then([](string_view seqText) -> Result<string> {
            const string owned(seqText);
            return parseMovements(owned)
          .transform([](const vector<ParsedMovement>& motions) {
            // All motion-parser output is TokenKind::Movement by construction.
            vector<string> rows;
            rows.reserve(motions.size());
            for (const auto& motion : motions) {
              ostringstream oss;
              oss << motion;
              rows.push_back(string{tokenKindChar(TokenKind::Movement)} + "\t" + oss.str());
            }
            return helpers::joinWithTrailingNewline(rows);
          })
          .transform_error([](const MovementParseError& error) {
            return ExportError{
                .kind = ExportErrorKind::InvalidValue,
                .message = formatMovementParseError(error),
            };
          });
          }));
}

VFByteSlice vf_tokenize_sequence(const char *seq, size_t seq_len) {
  static string result_storage;
  return helpers::storeBytes(result_storage,
      helpers::requiredBytes(seq, seq_len, "seq")
          .and_then([](string_view seqText) {
            return parseSequence(seqText)
          .transform([](const vector<TaggedToken>& tokens) {
            vector<string> rows;
            rows.reserve(tokens.size());
            for (const auto& tok : tokens) {
              rows.push_back(string{tokenKindChar(tok.kind)} + "\t" + tok.token);
            }
            return helpers::joinWithTrailingNewline(rows);
          })
          .transform_error([](const SequenceParseError& error) {
            return ExportError{
                .kind = ExportErrorKind::InvalidValue,
                .message = formatSequenceParseError(error),
            };
          });
          }));
}

VFByteSlice vf_build_sequence(const char *encoded_events, size_t encoded_events_len) {
  static string result_storage;
  return helpers::storeBytes(result_storage,
      helpers::requiredBytes(encoded_events, encoded_events_len, "encoded_events")
          .and_then(logic::buildKeySequence));
}

VFByteSlice vf_compute_search_region(
    const char *encoded_start_lines,
    size_t encoded_start_lines_len,
    const char *encoded_end_lines,
    size_t encoded_end_lines_len,
    int start_row,
    int end_row,
    int padding) {
  static string result_storage;
  return helpers::storeBytes(result_storage,
      helpers::requiredBytes(
          encoded_start_lines,
          encoded_start_lines_len,
          "encoded_start_lines")
          .and_then(payload::decodeLineArray)
          .and_then([&](const Lines& startLines) {
            return helpers::requiredBytes(
                    encoded_end_lines,
                    encoded_end_lines_len,
                    "encoded_end_lines")
                .and_then(payload::decodeLineArray)
                .transform([&](const Lines& endLines) {
                  const auto [regionStart, regionEnd] = logic::computeSearchRegion(
                      start_row, end_row, startLines, endLines, padding);
                  return helpers::packInts(regionStart, regionEnd);
                });
          }));
}

int vf_resolve_recall_cutoff(
    const char *encoded_records,
    size_t encoded_records_len,
    int64_t target_hrtime,
    int budget) {
  CHECK(budget >= 0, "budget must be non-negative");
  return helpers::storeIntOr(0,
      helpers::requiredBytes(encoded_records, encoded_records_len, "encoded_records")
          .and_then(payload::decodeRecallRecordMeta)
          .transform([&](const vector<payload::RecallRecordMeta>& records) {
            return logic::resolveRecallCutoffIndex(records, target_hrtime, budget);
          }));
}

int vf_manual_evict_reason(
    int start_row,
    int cursor_row,
    int64_t last_key_time_ns,
    bool has_last_key,
    int64_t now_ns,
    int max_search_lines,
    int manual_idle_timeout_seconds) {
  CHECK(max_search_lines >= 0, "max_search_lines must be non-negative");
  CHECK(manual_idle_timeout_seconds >= 0, "manual_idle_timeout_seconds must be non-negative");
  return logic::manualEvictReason(
      start_row,
      cursor_row,
      last_key_time_ns,
      has_last_key,
      now_ns,
      max_search_lines,
      manual_idle_timeout_seconds);
}

VFByteSlice vf_format_sequence(const char *seq, size_t seq_len) {
  static string result_storage;
  return helpers::storeBytes(result_storage,
      helpers::requiredBytes(seq, seq_len, "seq")
          .transform([](string_view seqText) {
            return formatSequenceForDisplay(seqText);
          }));
}

// Lua settings_schema consumes these defaults without mirroring C++ values.
VFByteSlice vf_get_optimizer_defaults() {
  static string cached;
  if (cached.empty()) {
    OptimizerParamsBase shared;
    NavOptimizerParams nav;
    TransformOptimizerParams tx;
    CompositionOptimizerParams comp;

    ostringstream out;
    auto i = [&](string_view scope, string_view key, int v) {
      out << scope << ':' << key << ":int=" << v << '\n';
    };
    auto d = [&](string_view scope, string_view key, double v) {
      out << scope << ':' << key << ":double=" << v << '\n';
    };
    auto b = [&](string_view scope, string_view key, bool v) {
      out << scope << ':' << key << ":bool=" << (v ? '1' : '0') << '\n';
    };

    // Shared
    i("shared", "maxResults",     shared.maxResults);
    i("shared", "maxNodesPopped", shared.maxNodesPopped);
    d("shared", "exploreFactor",  shared.exploreFactor);
    d("shared", "effortWeight",   shared.effortWeight);
    d("shared", "distanceWeight", shared.distanceWeight);
    i("shared", "minPrefixCount", shared.minPrefixCount);
    i("shared", "maxPrefixCount", shared.maxPrefixCount);

    // Nav
    i("nav", "maxResults",            nav.maxResults);
    i("nav", "maxNodesPopped",        nav.maxNodesPopped);
    d("nav", "exploreFactor",         nav.exploreFactor);
    d("nav", "effortWeight",          nav.effortWeight);
    d("nav", "distanceWeight",        nav.distanceWeight);
    i("nav", "minPrefixCount",        nav.minPrefixCount);
    i("nav", "maxPrefixCount",        nav.maxPrefixCount);
    i("nav", "fMotionThreshold",      nav.fMotionThreshold);
    b("nav", "useDirectionalPruning", nav.useDirectionalPruning);
    i("nav", "maxResultsPerEndPos",   nav.maxResultsPerEndPos);

    // Transform
    i("transform", "maxResults",            tx.maxResults);
    i("transform", "maxNodesPopped",        tx.maxNodesPopped);
    d("transform", "exploreFactor",         tx.exploreFactor);
    d("transform", "effortWeight",          tx.effortWeight);
    d("transform", "distanceWeight",        tx.distanceWeight);
    i("transform", "minPrefixCount",        tx.minPrefixCount);
    i("transform", "maxPrefixCount",        tx.maxPrefixCount);
    i("transform", "maxResultsPerStartPos", tx.maxResultsPerStartPos);

    // Composition
    i("composition", "maxResults",            comp.maxResults);
    i("composition", "maxNodesPopped",        comp.maxNodesPopped);
    d("composition", "exploreFactor",         comp.exploreFactor);
    d("composition", "effortWeight",          comp.effortWeight);
    d("composition", "distanceWeight",        comp.distanceWeight);
    i("composition", "minPrefixCount",        comp.minPrefixCount);
    i("composition", "maxPrefixCount",        comp.maxPrefixCount);
    i("composition", "fMotionThreshold",      comp.fMotionThreshold);
    b("composition", "useDirectionalPruning", comp.useDirectionalPruning);
    d("composition", "overshootPenalty",      comp.overshootPenalty);
    d("composition", "moveDeleteScale",       comp.moveDeleteScale);
    i("composition", "navPaddingAbove",       comp.navPaddingAbove);
    i("composition", "navPaddingBelow",       comp.navPaddingBelow);
    i("composition", "diffAlgorithm",         comp.diffAlgorithm);

    cached = out.str();
  }
  return helpers::byteSlice(cached);
}

VFByteSlice vf_simulate_movements(
    const char *encoded_lines,
    size_t encoded_lines_len,
    int start_row,
    int start_col,
    const char *seq,
    size_t seq_len) {
  static string result_storage;
  return helpers::storeBytes(result_storage,
      helpers::requiredBytes(encoded_lines, encoded_lines_len, "encoded_lines")
          .and_then(payload::decodeLineArray)
          .and_then([&](const Lines& lines) {
            return helpers::requiredBytes(seq, seq_len, "seq")
                .and_then([&](string_view movementSeq) -> Result<std::string> {
                  // Validate before simulateMovements (which .value()s the
                  // parse): an exception must never cross this extern "C" ABI.
                  if (!parseMovements(movementSeq)) {
                    return helpers::unexpectedError(
                        ExportErrorKind::InvalidValue, "invalid movement sequence");
                  }
                  CursorPos pos(start_row, start_col);
                  const CursorPos landed = simulateMovements(pos, movementSeq, lines, NavContext());
                  return helpers::packInts(landed.line, landed.col);
                });
          }));
}

}
