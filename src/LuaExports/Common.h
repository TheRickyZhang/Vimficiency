#pragma once

#include "LuaExports/Api.h"
#include "Keyboard/Config.h"
#include "Keyboard/Finger.h"
#include "Keyboard/Hand.h"
#include "Keyboard/Key.h"
#include "Optimizer/CountPenalty.h"
#include "Types/Lines.h"
#include "Utils/CoutCapture.h"
#include <cstdint>
#include <expected>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Cross-cutting declarations and inline helpers shared by every LuaExports/*.cpp.
// Out-of-line definitions for `payload::*` live in Payload.cpp; for `logic::*`
// in Logic.cpp. FFI export bodies live in *Exports.cpp.
namespace VF::LuaExports {

inline constexpr char EVENT_FIELD_SEP = '\x1f';
inline constexpr char EVENT_RECORD_SEP = '\x1e';
inline constexpr int MANUAL_EVICT_NONE = 0;
inline constexpr int MANUAL_EVICT_DRIFT = 1;
inline constexpr int MANUAL_EVICT_IDLE = 2;

enum class ExportErrorKind {
  MissingInput,
  InvalidPayload,
  PayloadTooLarge,
  TruncatedPayload,
  InvalidValue,
};

struct ExportError {
  ExportErrorKind kind;
  std::string message;
};

template <typename T>
using Result = std::expected<T, ExportError>;

enum DEFAULT_KEYBOARD {
  NONE,
  UNIFORM,
  QWERTY,
  COLEMAK_DH,
};

extern VFConfig g_config_ffi;
extern Config g_config_internal;

namespace helpers {

inline std::unexpected<ExportError> unexpectedError(
    ExportErrorKind kind,
    std::string message) {
  return std::unexpected<ExportError>{ ExportError{
      .kind = kind,
      .message = std::move(message),
  } };
}

inline Result<std::string_view> requiredBytes(
    const char* data,
    size_t len,
    std::string_view name) {
  if (!data && len > 0) {
    return unexpectedError(
        ExportErrorKind::MissingInput,
        "missing " + std::string(name));
  }
  return data ? std::string_view(data, len) : std::string_view{};
}

inline VFByteSlice byteSlice(const std::string& storage) {
  return VFByteSlice{
      .data = storage.data(),
      .len = storage.size(),
  };
}

inline VFByteSlice storeBytes(std::string& storage, Result<std::string> result) {
  if (!result) {
    storage = "ERROR: " + result.error().message;
    return byteSlice(storage);
  }
  storage = std::move(*result);
  return byteSlice(storage);
}

inline int storeIntOr(int fallback, Result<int> result) {
  if (!result) return fallback;
  return *result;
}

inline std::string packInts(int first, int second) {
  return std::to_string(first) + std::string(1, EVENT_FIELD_SEP) + std::to_string(second);
}

inline std::string joinWithTrailingNewline(const std::vector<std::string>& items) {
  std::ostringstream oss;
  for (const auto& item : items) {
    oss << item << "\n";
  }
  return oss.str();
}

}  // namespace helpers

namespace payload {

// Wire format: a sequence of "<len>:<bytes>" chunks. Encoders and decoders
// are paired so adding a new field type requires touching one place.

inline std::string encodeField(std::string_view field) {
  return std::to_string(field.size()) + ":" + std::string(field);
}

template <typename... Fields>
std::string encodeFields(const Fields&... fields) {
  std::string out;
  ((out += encodeField(fields)), ...);
  return out;
}

inline std::string intField(int v) { return std::to_string(v); }

inline std::string doubleField(double v) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(6);
  oss << v;
  return oss.str();
}

struct KeyTrackingEvent {
  std::string mode;
  std::string keyTyped;
};

struct RecallRecordMeta {
  int64_t timeStarted = 0;
  std::string firstMode;
};

Result<std::vector<std::string>> decodeLengthPrefixedStrings(std::string_view encoded);
Result<Lines> decodeLineArray(std::string_view encoded);
Result<std::vector<RecallRecordMeta>> decodeRecallRecordMeta(std::string_view encoded);
Result<std::vector<KeyTrackingEvent>> decodeKeyTrackingEvents(std::string_view encoded);

}  // namespace payload

namespace logic {

Result<std::string> buildKeySequence(std::string_view encoded);
std::pair<int, int> computeSearchRegion(
    int startRow,
    int endRow,
    const Lines& startLines,
    const Lines& endLines,
    int padding);
int resolveRecallCutoffIndex(
    const std::vector<payload::RecallRecordMeta>& records,
    int64_t targetHrtime,
    int budget);
int manualEvictReason(
    int startRow,
    int cursorRow,
    int64_t lastKeyTimeNs,
    bool hasLastKey,
    int64_t nowNs,
    int maxSearchLines,
    int manualIdleTimeoutSeconds);

}  // namespace logic

void sync_config();

}  // namespace VF::LuaExports
