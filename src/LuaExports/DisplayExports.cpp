#include "LuaExports/Common.h"

#include "Interpreter/SequenceDisplay.h"
#include "Session/ResultView.h"

#include <charconv>
#include <string>

using namespace std;
namespace helpers = VF::LuaExports::helpers;
namespace payload = VF::LuaExports::payload;
using VF::LuaExports::ExportErrorKind;
using VF::LuaExports::Result;

namespace {

Result<int> parseIntField(string_view text, string_view name) {
  int out = 0;
  const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out);
  if (ec != errc{} || ptr != text.data() + text.size()) {
    return helpers::unexpectedError(
        ExportErrorKind::InvalidValue,
        "invalid " + string(name));
  }
  return out;
}

Result<double> parseDoubleField(string_view text, string_view name) {
  double out = 0;
  const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out);
  if (ec != errc{} || ptr != text.data() + text.size()) {
    return helpers::unexpectedError(
        ExportErrorKind::InvalidValue,
        "invalid " + string(name));
  }
  return out;
}

string encodeStringList(const vector<string>& values) {
  string out;
  for (const string& value : values) {
    out += payload::encodeField(value);
  }
  return out;
}

struct PositionFields {
  int startRow = 0;
  int startCol = 0;
  int endRow = 0;
  int endCol = 0;
};

Result<PositionFields> parsePositionFields(const vector<string>& fields) {
  auto startRow = parseIntField(fields[1], "start_row");
  if (!startRow) return unexpected(startRow.error());
  auto startCol = parseIntField(fields[2], "start_col");
  if (!startCol) return unexpected(startCol.error());
  auto endRow = parseIntField(fields[3], "end_row");
  if (!endRow) return unexpected(endRow.error());
  auto endCol = parseIntField(fields[4], "end_col");
  if (!endCol) return unexpected(endCol.error());

  return PositionFields{
      .startRow = *startRow,
      .startCol = *startCol,
      .endRow = *endRow,
      .endCol = *endCol,
  };
}

Result<VF::SessionView::Result> decodeSessionResult(string_view encoded) {
  return payload::decodeLengthPrefixedStrings(encoded)
      .and_then([](const vector<string>& fields)
          -> Result<VF::SessionView::Result> {
        if (fields.size() != 10) {
          return helpers::unexpectedError(
              ExportErrorKind::InvalidPayload,
              "invalid session result display payload");
        }

        VF::SessionView::Result result;
        auto lines = payload::decodeLengthPrefixedStrings(fields[0]);
        if (!lines) return unexpected(lines.error());
        result.lines = std::move(*lines);

        auto position = parsePositionFields(fields);
        if (!position) return unexpected(position.error());
        const auto [startRow, startCol, endRow, endCol] = *position;
        result.startRow = startRow;
        result.startCol = startCol;
        result.endRow = endRow;
        result.endCol = endCol;
        result.userSeq = fields[5];

        if (fields[6] == "1") {
          auto userCost = parseDoubleField(fields[7], "user_cost");
          if (!userCost) return unexpected(userCost.error());
          result.userCost = *userCost;
        } else if (fields[6] != "0") {
          return helpers::unexpectedError(
              ExportErrorKind::InvalidValue,
              "invalid user_cost presence flag");
        }

        result.finishReason = fields[8];

        auto recommendationFields =
            payload::decodeLengthPrefixedStrings(fields[9]);
        if (!recommendationFields) {
          return unexpected(recommendationFields.error());
        }
        if (recommendationFields->size() % 2 != 0) {
          return helpers::unexpectedError(
              ExportErrorKind::InvalidPayload,
              "invalid recommendation payload");
        }
        for (size_t i = 0; i < recommendationFields->size(); i += 2) {
          auto cost = parseDoubleField((*recommendationFields)[i + 1],
                                       "recommendation cost");
          if (!cost) return unexpected(cost.error());
          result.optimalResults.push_back({
              .seq = (*recommendationFields)[i],
              .cost = *cost,
          });
        }
        return result;
      });
}

Result<string> displayLinesImpl(
    const char* seq,
    size_t seqLen,
    bool tokenize,
    bool sectionize) {
  return helpers::requiredBytes(seq, seqLen, "seq")
      .transform([&](string_view seqText) {
        return encodeStringList(VF::SequenceDisplay::lines(
            seqText,
            {.tokenize = tokenize, .sectionize = sectionize}));
      });
}

Result<string> displayKeyNotationImpl(const char* text, size_t textLen) {
  return helpers::requiredBytes(text, textLen, "text")
      .transform([](string_view textValue) {
        return VF::SequenceDisplay::displayKeyNotation(textValue);
      });
}

Result<string> typedChunksImpl(
    const char* encodedChunks,
    size_t encodedChunksLen,
    bool tokenize) {
  return helpers::requiredBytes(
      encodedChunks,
      encodedChunksLen,
      "encoded_chunks")
      .and_then([](string_view encoded) {
        return payload::decodeLengthPrefixedStrings(encoded);
      })
      .and_then([&](const vector<string>& fields) -> Result<string> {
        if (fields.size() % 2 != 0) {
          return helpers::unexpectedError(
              ExportErrorKind::InvalidPayload,
              "invalid typed chunk payload");
        }

        vector<VF::SequenceDisplay::TypedChunk> chunks;
        chunks.reserve(fields.size() / 2);
        for (size_t i = 0; i < fields.size(); i += 2) {
          chunks.push_back({
              .kind = fields[i],
              .text = fields[i + 1],
          });
        }
        return VF::SequenceDisplay::typedChunksInline(chunks, tokenize);
      });
}

Result<string> formatPositionImpl(
    int startRow,
    int startCol,
    int endRow,
    int endCol) {
  return VF::SessionView::formatPosition(VF::SessionView::Result{
      .startRow = startRow,
      .startCol = startCol,
      .endRow = endRow,
      .endCol = endCol,
  });
}

Result<string> formatReasonSuffixImpl(const char* reason, size_t reasonLen) {
  return helpers::requiredBytes(reason, reasonLen, "finish_reason")
      .transform([](string_view reasonText) {
        return VF::SessionView::formatReasonSuffix(
            VF::SessionView::Result{.finishReason = string(reasonText)});
      });
}

Result<string> formatBodyImpl(
    const char* encodedResult,
    size_t encodedResultLen,
    bool tokenize,
    bool sectionize) {
  return helpers::requiredBytes(
      encodedResult,
      encodedResultLen,
      "encoded_result")
      .and_then(decodeSessionResult)
      .transform([&](const VF::SessionView::Result& result) {
        return encodeStringList(VF::SessionView::formatBody(
            result,
            {.tokenize = tokenize, .sectionize = sectionize}));
      });
}

Result<string> formatMessageImpl(
    const char* title,
    size_t titleLen,
    const char* encodedResult,
    size_t encodedResultLen,
    bool tokenize,
    bool sectionize) {
  return helpers::requiredBytes(title, titleLen, "title")
      .and_then([&](string_view titleText) {
        return helpers::requiredBytes(
            encodedResult,
            encodedResultLen,
            "encoded_result")
            .and_then(decodeSessionResult)
            .transform([&](const VF::SessionView::Result& result) {
              return VF::SessionView::formatMessage(
                  titleText,
                  result,
                  {.tokenize = tokenize, .sectionize = sectionize});
            });
      });
}

Result<string> formatSavedLinesImpl(
    const char* name,
    size_t nameLen,
    const char* encodedResult,
    size_t encodedResultLen,
    bool tokenize,
    bool sectionize) {
  return helpers::requiredBytes(name, nameLen, "name")
      .and_then([&](string_view nameText) {
        return helpers::requiredBytes(
            encodedResult,
            encodedResultLen,
            "encoded_result")
            .and_then(decodeSessionResult)
            .transform([&](const VF::SessionView::Result& result) {
              return encodeStringList(VF::SessionView::formatSavedLines(
                  nameText,
                  result,
                  {.tokenize = tokenize, .sectionize = sectionize}));
            });
      });
}

}  // namespace

extern "C" {

VFByteSlice vf_sequence_display_lines(
    const char* seq,
    size_t seq_len,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      displayLinesImpl(seq, seq_len, tokenize, sectionize));
}

VFByteSlice vf_sequence_display_inline(
    const char* seq,
    size_t seq_len,
    bool tokenize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      helpers::requiredBytes(seq, seq_len, "seq")
          .transform([&](string_view seqText) {
            return VF::SequenceDisplay::inlineText(
                seqText,
                {.tokenize = tokenize, .sectionize = false});
          }));
}

VFByteSlice vf_sequence_display_key_notation(
    const char* text,
    size_t text_len) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      displayKeyNotationImpl(text, text_len));
}

VFByteSlice vf_sequence_display_typed_text(
    const char* text,
    size_t text_len,
    bool key_notation) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      helpers::requiredBytes(text, text_len, "text")
          .transform([&](string_view textValue) {
            return key_notation
                ? VF::SequenceDisplay::displayTypedText(textValue)
                : VF::SequenceDisplay::displayLiteralTypedText(textValue);
          }));
}

VFByteSlice vf_sequence_display_typed_chunks(
    const char* encoded_chunks,
    size_t encoded_chunks_len,
    bool tokenize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      typedChunksImpl(encoded_chunks, encoded_chunks_len, tokenize));
}

VFByteSlice vf_format_result_position(
    int start_row,
    int start_col,
    int end_row,
    int end_col) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      formatPositionImpl(start_row, start_col, end_row, end_col));
}

VFByteSlice vf_format_result_reason_suffix(
    const char* reason,
    size_t reason_len) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      formatReasonSuffixImpl(reason, reason_len));
}

VFByteSlice vf_format_result_body(
    const char* encoded_result,
    size_t encoded_result_len,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      formatBodyImpl(encoded_result, encoded_result_len, tokenize, sectionize));
}

VFByteSlice vf_format_result_message(
    const char* title,
    size_t title_len,
    const char* encoded_result,
    size_t encoded_result_len,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      formatMessageImpl(
          title,
          title_len,
          encoded_result,
          encoded_result_len,
          tokenize,
          sectionize));
}

VFByteSlice vf_format_saved_result_lines(
    const char* name,
    size_t name_len,
    const char* encoded_result,
    size_t encoded_result_len,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      formatSavedLinesImpl(
          name,
          name_len,
          encoded_result,
          encoded_result_len,
          tokenize,
          sectionize));
}

}
