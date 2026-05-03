#include "LuaExports/Common.h"

#include "AbiHash.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Utils/Debug.h"
#include "VimCore/VimOptions.h"

namespace VF::LuaExports {

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
static_assert(sizeof(VFConfig{}.keys) / sizeof(VFKeyInfo) == KEY_COUNT,
              "C ABI key array size must match Key enum");
static_assert(
    sizeof(VFConfig{}.count_penalty_overrides) / sizeof(VFCountPenaltyOverride)
        == CountClassCOUNT,
    "C ABI count penalty override array size must match CountClass enum");

VFConfig defaultConfigFfi() {
  VFConfig config{};
  config.default_keyboard = UNIFORM;
  config.weights.keyWeight = 1.0;
  config.shiftwidth = -1;
  for (size_t i = 0; i < KEY_COUNT; i++) {
    config.keys[i].hand = static_cast<int8_t>(Hand::None);
    config.keys[i].finger = static_cast<int8_t>(Finger::None);
  }
  return config;
}

VFConfig g_config_ffi = defaultConfigFfi();
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

  g_config_internal.weights.w_key = g_config_ffi.weights.keyWeight;
  g_config_internal.weights.w_same_finger = g_config_ffi.weights.sameFingerWeight;
  g_config_internal.weights.w_same_key = g_config_ffi.weights.sameKeyWeight;
  g_config_internal.weights.w_alt_bonus = g_config_ffi.weights.altHandWeight;
  g_config_internal.weights.w_roll_good = g_config_ffi.weights.goodRollWeight;
  g_config_internal.weights.w_roll_bad = g_config_ffi.weights.badRollWeight;

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

}  // namespace VF::LuaExports

using namespace VF::LuaExports;

extern "C" {

extern const int VF_KEY_COUNT = KEY_COUNT;
extern const int VF_FINGER_COUNT = FINGER_COUNT;
extern const int VF_HAND_COUNT = HAND_COUNT;
extern const int VF_COUNT_CLASS_COUNT = static_cast<int>(CountClassCOUNT);

VFConfig *vf_get_config() { return &g_config_ffi; }
void vf_apply_config() { sync_config(); }

const char *vf_key_name(int index) {
  if (index < 0 || index >= VF_KEY_COUNT) return nullptr;
  return g_key_names[index];
}

const char *vf_hand_name(int index) {
  if (index < 0 || index >= VF_HAND_COUNT) return nullptr;
  return g_hand_names[index];
}

const char *vf_finger_name(int index) {
  if (index < 0 || index >= VF_FINGER_COUNT) return nullptr;
  return g_finger_names[index];
}

const char *vf_count_class_name(int index) {
  if (index < 0 || index >= VF_COUNT_CLASS_COUNT) return nullptr;
  return g_count_class_names[index];
}

const char* vf_get_debug() {
  static std::string debug_storage;
  debug_storage = consume_debug_output();
  return debug_storage.c_str();
}

const char* vf_get_warnings() {
  static std::string warning_storage;
  warning_storage = consume_warning_output();
  return warning_storage.c_str();
}

int vf_version() { return 1; }

const char *vf_abi_hash() { return VF_ABI_HASH; }

const char *vf_debug_config() {
  static std::string debug_storage;
  std::ostringstream oss;

  oss << "=== VFConfig Debug ===\n";
  oss << "default_keyboard: " << g_config_ffi.default_keyboard << "\n";
  oss << "\n--- Weights (FFI) ---\n";
  oss << "keyWeight: " << g_config_ffi.weights.keyWeight << "\n";
  oss << "sameFingerWeight: " << g_config_ffi.weights.sameFingerWeight << "\n";
  oss << "sameKeyWeight: " << g_config_ffi.weights.sameKeyWeight << "\n";
  oss << "altHandWeight: " << g_config_ffi.weights.altHandWeight << "\n";

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
