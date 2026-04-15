#pragma once

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

namespace vimficiency::lua_exports {

inline constexpr char kEventFieldSep = '\x1f';
inline constexpr char kEventRecordSep = '\x1e';
inline constexpr int kManualEvictNone = 0;
inline constexpr int kManualEvictDrift = 1;
inline constexpr int kManualEvictIdle = 2;

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

struct C_ScoreWeights {
  double w_key = 1.0;
  double w_same_finger{};
  double w_same_key{};
  double w_alt_bonus{};
  double w_roll_good{};
  double w_roll_bad{};
};

struct C_KeyInfo {
  int8_t hand = static_cast<int8_t>(Hand::None);
  int8_t finger = static_cast<int8_t>(Finger::None);
  double base_cost = 0.0;
};

struct C_CountPenaltyOverride {
  bool has_base = false;
  double base = 0.0;
  bool has_count_slope = false;
  double count_slope = 0.0;
  bool has_span_slope = false;
  double span_slope = 0.0;
};

struct VimficiencyConfigFFI {
  DEFAULT_KEYBOARD default_keyboard = UNIFORM;
  C_ScoreWeights weights{};
  C_KeyInfo keys[KEY_COUNT]{};
  int slice_buffer_amount{};
  int32_t shiftwidth = -1;
  bool use_count_penalty_overrides = false;
  C_CountPenaltyOverride count_penalty_overrides[CountClassCOUNT]{};
};

extern VimficiencyConfigFFI g_config_ffi;
extern Config g_config_internal;

namespace export_helpers {

inline std::string_view optionalText(const char* text) {
  return text ? std::string_view(text) : std::string_view{};
}

inline std::unexpected<ExportError> unexpectedError(
    ExportErrorKind kind,
    std::string message) {
  return std::unexpected<ExportError>{ ExportError{
      .kind = kind,
      .message = std::move(message),
  } };
}

inline Result<std::string_view> requiredText(const char* text, std::string_view name) {
  if (!text) {
    return unexpectedError(
        ExportErrorKind::MissingInput,
        "missing " + std::string(name));
  }
  return text;
}

template <typename Compute>
const char* storeString(std::string& storage, Compute compute) {
  // FFI exports currently assume single-threaded Neovim calls, so a
  // per-export static backing string is sufficient for returned `const char*`.
  auto result = compute();
  if (!result) {
    storage = std::string("ERROR: ") + result.error().message;
    return storage.c_str();
  }
  storage = std::move(*result);
  return storage.c_str();
}

template <typename Compute>
const char* storeOptionalString(std::string& storage, const char* text, Compute compute) {
  const std::string_view input = optionalText(text);
  if (input.empty()) {
    storage.clear();
    return storage.c_str();
  }
  return storeString(storage, [&] { return compute(input); });
}

template <typename Compute>
int storeIntOr(int fallback, Compute compute) {
  auto result = compute();
  if (!result) {
    return fallback;
  }
  return *result;
}

inline std::string packInts(int first, int second) {
  return std::to_string(first) + std::string(1, kEventFieldSep) + std::to_string(second);
}

inline std::string joinWithTrailingNewline(const std::vector<std::string>& items) {
  std::ostringstream oss;
  for (const auto& item : items) {
    oss << item << "\n";
  }
  return oss.str();
}

}  // namespace export_helpers

namespace payload {

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

namespace pure_logic {

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

}  // namespace pure_logic

void sync_config();

}  // namespace vimficiency::lua_exports
