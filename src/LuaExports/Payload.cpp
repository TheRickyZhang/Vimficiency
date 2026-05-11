#include "LuaExports/Common.h"

#include <charconv>
#include <limits>

using namespace std;

namespace VF::LuaExports::payload {

Result<vector<string>> decodeLengthPrefixedStrings(string_view encoded) {
  vector<string> parts;
  size_t pos = 0;
  while (pos < encoded.size()) {
    const size_t colon = encoded.find(':', pos);
    if (colon == string_view::npos) {
      return helpers::unexpectedError(
          ExportErrorKind::InvalidPayload,
          "invalid length-prefixed payload");
    }
    const string_view lenText = encoded.substr(pos, colon - pos);
    if (lenText.empty()) {
      return helpers::unexpectedError(
          ExportErrorKind::InvalidPayload,
          "invalid length-prefixed payload");
    }
    size_t len = 0;
    for (char c : lenText) {
      if (c < '0' || c > '9') {
        return helpers::unexpectedError(
            ExportErrorKind::InvalidPayload,
            "invalid length-prefixed payload");
      }
      const size_t digit = static_cast<size_t>(c - '0');
      if (len > (numeric_limits<size_t>::max() - digit) / 10) {
        return helpers::unexpectedError(
            ExportErrorKind::PayloadTooLarge,
            "length-prefixed payload too large");
      }
      len = len * 10 + digit;
    }
    pos = colon + 1;
    if (pos + len > encoded.size()) {
      return helpers::unexpectedError(
          ExportErrorKind::TruncatedPayload,
          "truncated length-prefixed payload");
    }
    parts.emplace_back(encoded.substr(pos, len));
    pos += len;
  }
  return parts;
}

Result<Lines> decodeLineArray(string_view encoded) {
  return decodeLengthPrefixedStrings(encoded).and_then([](const vector<string>& parts)
      -> Result<Lines> {
    if (parts.empty()) {
      return helpers::unexpectedError(
          ExportErrorKind::InvalidPayload,
          "line array payload must contain at least one line");
    }
    Lines lines;
    lines.reserve(parts.size());
    for (size_t i = 0; i < parts.size(); ++i) {
      const auto& part = parts[i];
      if (part.find('\n') != string::npos) {
        return helpers::unexpectedError(
            ExportErrorKind::InvalidPayload,
            string("line array payload contains newline byte at line ") + to_string(i));
      }
      if (part.find('\0') != string::npos) {
        return helpers::unexpectedError(
            ExportErrorKind::InvalidPayload,
            string("line array payload contains NUL byte at line ") + to_string(i));
      }
      lines.push_back(part);
    }
    return lines;
  });
}

Result<vector<RecallRecordMeta>> decodeRecallRecordMeta(string_view encoded) {
  return decodeLengthPrefixedStrings(encoded).and_then([](const vector<string>& parts)
      -> Result<vector<RecallRecordMeta>> {
    if (parts.size() % 2 != 0) {
      return helpers::unexpectedError(
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
        return helpers::unexpectedError(
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
        return helpers::unexpectedError(
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

}  // namespace VF::LuaExports::payload
