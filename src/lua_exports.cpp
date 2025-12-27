// src/lua_exports.cpp

#include "Keyboard/KeyboardModel.h"
#include "Keyboard/XMacroKeyDefinitions.h"
#include "Optimizer/Config.h"
#include "Optimizer/Optimizer.h"
#include "State/State.h"
#include "Utils/CoutCapture.h"
#include "Utils/Debug.h"
#include <sstream>
#include <string>
#include <vector>


// Generate name arrays from same source
#define STRING_VALUE(name, str) str,
static const char *g_key_names[] = {VIMFICIENCY_KEYS(STRING_VALUE)};
static const char *g_hand_names[] = {VIMFICIENCY_HANDS(STRING_VALUE)};
static const char *g_finger_names[] = {VIMFICIENCY_FINGERS(STRING_VALUE)};
#undef STRING_VALUE

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
  double w_run_pen{};
  double w_roll_good{};
  double w_roll_bad{};
};

struct C_KeyInfo {
  int8_t hand = static_cast<int8_t>(Hand::None);
  int8_t finger = static_cast<int8_t>(Finger::None);
  double base_cost = 0.0;
};

struct VimficiencyConfigFFI {
  DEFAULT_KEYBOARD default_keyboard = UNIFORM;
  C_ScoreWeights weights{};
  C_KeyInfo keys[KEY_COUNT]{};
};

// Helper to split string by newlines
static std::vector<std::string> split_lines(const char *text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

// Global storage
static VimficiencyConfigFFI g_config_ffi; // Default to uniform
static Config g_config_internal = Config::uniform();

static void sync_config() {
  // We don't break, because additional key config should override on top of
  // defaults.
  switch (g_config_ffi.default_keyboard) {
  case QWERTY:
    g_config_internal = Config::qwerty();
    break;
  case COLEMAK_DH:
    g_config_internal = Config::colemak_dh();
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
  g_config_internal.weights.w_run_pen = g_config_ffi.weights.w_run_pen;
  g_config_internal.weights.w_roll_good = g_config_ffi.weights.w_roll_good;
  g_config_internal.weights.w_roll_bad = g_config_ffi.weights.w_roll_bad;

  // g_config_internal.weights = g_config_ffi.c_scoreWeights; // TODO: good idea
  // to overload this operator= ?
  for (size_t i = 0; i < KEY_COUNT; i++) {
    auto &src = g_config_ffi.keys[i];
    auto &dst = g_config_internal.keyInfo[i];

    if (src.hand != static_cast<int8_t>(Hand::None)) {
      dst.hand = static_cast<Hand>(src.hand);
      dst.finger = static_cast<Finger>(src.finger);
      dst.base_cost = src.base_cost;
    }
  }
}

extern "C" {
// Note we need to "redefine" (export) these since original declarations are
// constexpr, and so may be inlined / name mangled
extern const int VIMFICIENCY_KEY_COUNT = KEY_COUNT;
extern const int VIMFICIENCY_FINGER_COUNT = FINGER_COUNT;
extern const int VIMFICIENCY_HAND_COUNT = HAND_COUNT;

VimficiencyConfigFFI *vimficiency_get_config() { return &g_config_ffi; }
void vimficiency_apply_config() { sync_config(); }

const char *vimficiency_key_name(int index) {
  if (index < 0 || index >= VIMFICIENCY_KEY_COUNT)
    return nullptr;
  return g_key_names[index];
}
const char *vimficiency_hand_name(int index) {
  if (index < 0 || index >= VIMFICIENCY_HAND_COUNT)
    return nullptr;
  return g_hand_names[index];
}
const char *vimficiency_finger_name(int index) {
  if (index < 0 || index >= VIMFICIENCY_FINGER_COUNT)
    return nullptr;
  return g_finger_names[index];
}

const char *vimficiency_analyze(const char *start_text, int start_row,
                                int start_col, const char *end_text,
                                int end_row, int end_col, const char *keyseq) {
  static std::string result_storage;

  auto start_lines = split_lines(start_text);
  auto end_lines = split_lines(end_text);

  Position start_position(start_row, start_col);
  Position end_position(end_row, end_col);

  State startingState(start_position, RunningEffort(), 0, 0);

  // g_config_internal was already populated by vimficiency_apply_config()
  Optimizer o(startingState, g_config_internal, 1);

  std::vector<Result> res = o.optimizeMovement(start_lines, end_position, keyseq);

  // Format results
  std::ostringstream oss;
  if (res.empty()) {
    oss << "no results";
  } else {
    for (const Result &r : res) {
      oss << r.sequence << " " << std::fixed << std::setprecision(3)
          << r.keyCost << "\n";
    }
  }

  if constexpr (DEBUG_ENABLED) {
    oss << "\n ----------------DEBUG---------------- \n" << get_debug_output();
  }

  result_storage = oss.str();
  return result_storage.c_str();
}

const char* vimficiency_get_debug() {
    static std::string debug_storage;
    debug_storage = get_debug_output();
    clear_debug_output();
    return debug_storage.c_str();
}

// Unimportant debug stuff
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

  oss << "\n--- Sample Keys (Internal) ---\n";
  // Show a few keys to verify
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
