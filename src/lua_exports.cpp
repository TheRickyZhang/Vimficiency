#include "LuaExports/Common.h"

#include "Optimizer/GlobalRuntimeOptions.h"
#include "Utils/Debug.h"
#include "VimCore/VimOptions.h"

namespace LuaExports {

static const char* g_key_names[] = {
#define X(name, str) str,
#include "Keyboard/XMacroKey.inc"
#undef X
};

static const char* g_hand_names[] = {
#define X(name, str) str,
#include "Keyboard/XMacroHand.inc"
#undef X
};

static const char* g_finger_names[] = {
#define X(name, str) str,
#include "Keyboard/XMacroFinger.inc"
#undef X
};

static const char* g_count_class_names[] = {
  "MovementChar",
  "MovementWord",
  "MovementWORD",
  "MovementLine",
  "MovementParagraph",
  "MovementSentence",
  "MovementJump",
  "EditChar",
  "EditWord",
  "EditWORD",
  "EditLine",
  "EditParagraph",
  "EditSentence",
  "Join",
};

static_assert(sizeof(g_count_class_names) / sizeof(g_count_class_names[0]) == CountClassCOUNT,
              "Count class name table must match CountClass enum");

VimficiencyConfigFFI g_config_ffi;
Config g_config_internal = Config::uniform();

void sync_config() {
  switch (g_config_ffi.default_keyboard) {
  case QWERTY:
    g_config_internal = Config::qwerty();
    break;
  case COLEMAK_DH:
    g_config_internal = Config::colemakDh();
    break;
  case UNIFORM:
    g_config_internal = Config::uniform();
    break;
  case NONE:
    break;
  }

  g_config_internal.weights.w_key = g_config_ffi.weights.w_key;
  g_config_internal.weights.w_same_finger = g_config_ffi.weights.w_same_finger;
  g_config_internal.weights.w_same_key = g_config_ffi.weights.w_same_key;
  g_config_internal.weights.w_alt_bonus = g_config_ffi.weights.w_alt_bonus;
  g_config_internal.weights.w_roll_good = g_config_ffi.weights.w_roll_good;
  g_config_internal.weights.w_roll_bad = g_config_ffi.weights.w_roll_bad;

  for (size_t i = 0; i < KEY_COUNT; i++) {
    auto &src = g_config_ffi.keys[i];
    auto &dst = g_config_internal.keyInfo[i];
    if (src.hand != static_cast<int8_t>(Hand::None)) {
      dst.hand = static_cast<Hand>(src.hand);
      dst.finger = static_cast<Finger>(src.finger);
      dst.base_cost = src.base_cost;
    }
  }

  if (g_config_ffi.shiftwidth >= 0) {
    VimOptions::shiftwidthRef() = g_config_ffi.shiftwidth;
  }

  auto& runtimeOptions = globalRuntimeOptions();
  runtimeOptions.useCountPenaltyOverrides = g_config_ffi.use_count_penalty_overrides;
  runtimeOptions.countPenaltyOverrides = {};
  for (size_t i = 0; i < CountClassCOUNT; i++) {
    const auto& src = g_config_ffi.count_penalty_overrides[i];
    PartialCountPenaltyParams dst;
    if (src.has_base) dst.base = src.base;
    if (src.has_count_slope) dst.countSlope = src.count_slope;
    if (src.has_span_slope) dst.spanSlope = src.span_slope;
    if (dst.base || dst.countSlope || dst.spanSlope) {
      runtimeOptions.countPenaltyOverrides[i] = dst;
    }
  }
}

}  // namespace LuaExports

using namespace LuaExports;

extern "C" {

extern const int VIMFICIENCY_KEY_COUNT = KEY_COUNT;
extern const int VIMFICIENCY_FINGER_COUNT = FINGER_COUNT;
extern const int VIMFICIENCY_HAND_COUNT = HAND_COUNT;
extern const int VIMFICIENCY_COUNT_CLASS_COUNT = static_cast<int>(CountClassCOUNT);

VimficiencyConfigFFI *vimficiency_get_config() { return &g_config_ffi; }
void vimficiency_apply_config() { sync_config(); }

const char *vimficiency_key_name(int index) {
  if (index < 0 || index >= VIMFICIENCY_KEY_COUNT) return nullptr;
  return g_key_names[index];
}

const char *vimficiency_hand_name(int index) {
  if (index < 0 || index >= VIMFICIENCY_HAND_COUNT) return nullptr;
  return g_hand_names[index];
}

const char *vimficiency_finger_name(int index) {
  if (index < 0 || index >= VIMFICIENCY_FINGER_COUNT) return nullptr;
  return g_finger_names[index];
}

const char *vimficiency_count_class_name(int index) {
  if (index < 0 || index >= VIMFICIENCY_COUNT_CLASS_COUNT) return nullptr;
  return g_count_class_names[index];
}

const char* vimficiency_get_debug() {
  static std::string debug_storage;
  debug_storage = consume_debug_output();
  return debug_storage.c_str();
}

const char *vimficiency_debug_config() {
  static std::string debug_storage;
  std::ostringstream oss;

  oss << "=== VimficiencyConfig Debug ===\n";
  oss << "default_keyboard: " << g_config_ffi.default_keyboard << "\n";
  oss << "\n--- Weights (FFI) ---\n";
  oss << "w_key: " << g_config_ffi.weights.w_key << "\n";
  oss << "w_same_finger: " << g_config_ffi.weights.w_same_finger << "\n";
  oss << "w_same_key: " << g_config_ffi.weights.w_same_key << "\n";
  oss << "w_alt_bonus: " << g_config_ffi.weights.w_alt_bonus << "\n";

  oss << "\n--- Weights (Internal) ---\n";
  oss << "w_key: " << g_config_internal.weights.w_key << "\n";
  oss << "w_same_finger: " << g_config_internal.weights.w_same_finger << "\n";
  oss << "\n--- Count Penalty Overrides ---\n";
  oss << "use_count_penalty_overrides: " << g_config_ffi.use_count_penalty_overrides << "\n";

  oss << "\n--- Sample Keys (Internal) ---\n";
  auto show_key = [&](Key k, const char *name) {
    auto &info = g_config_internal.keyInfo[static_cast<size_t>(k)];
    oss << name << ": hand=" << static_cast<int>(info.hand)
        << " finger=" << static_cast<int>(info.finger)
        << " cost=" << info.base_cost << "\n";
  };
  show_key(Key::Key_Space, "Space");
  show_key(Key::Key_J, "J");
  show_key(Key::Key_K, "K");

  debug_storage = oss.str();
  return debug_storage.c_str();
}

}
