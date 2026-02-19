#include <gtest/gtest.h>

#include <dlfcn.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Keyboard/KeyboardModel.h"
#include "Optimizer/CountPenalty.h"

struct C_ScoreWeights {
  double w_key;
  double w_same_finger;
  double w_same_key;
  double w_alt_bonus;
  double w_roll_good;
  double w_roll_bad;
};

struct C_KeyInfo {
  int8_t hand;
  int8_t finger;
  double base_cost;
};

struct C_CountPenaltyOverride {
  bool has_base;
  double base;
  bool has_count_slope;
  double count_slope;
  bool has_span_slope;
  double span_slope;
};

struct VimficiencyConfigFFI {
  int default_keyboard;
  C_ScoreWeights weights;
  C_KeyInfo keys[KEY_COUNT];
  int slice_buffer_amount;
  int32_t shiftwidth;
  bool use_count_penalty_overrides;
  C_CountPenaltyOverride count_penalty_overrides[CountClassCOUNT];
};

namespace {
void* openVimficiencyLib() {
  std::array<char, 4096> exePath{};
  ssize_t n = readlink("/proc/self/exe", exePath.data(), exePath.size() - 1);
  std::filesystem::path exe = (n > 0) ? std::filesystem::path(std::string(exePath.data(), n))
                                      : std::filesystem::path();
  std::filesystem::path fromExe =
      exe.empty() ? std::filesystem::path() : exe.parent_path().parent_path() / "libvimficiency.so";

  const std::vector<std::string> candidates = {
      fromExe.empty() ? "" : fromExe.string(),
      (std::filesystem::current_path() / "build/libvimficiency.so").string(),
      "libvimficiency.so",
  };

  for (const auto& c : candidates) {
    if (c.empty()) continue;
    if (void* h = dlopen(c.c_str(), RTLD_NOW)) return h;
  }
  return nullptr;
}

std::unordered_map<std::string, double> parseBySequence(const std::string& analyzeOut) {
  std::unordered_map<std::string, double> bySeq;
  std::istringstream iss(analyzeOut);
  std::string line;
  bool headerSeen = false;
  while (std::getline(iss, line)) {
    if (line.empty()) continue;
    if (!headerSeen) {
      headerSeen = true;
      continue;
    }
    std::istringstream row(line);
    std::string seq;
    double cost = 0.0;
    if (row >> seq >> cost) {
      bySeq.emplace(seq, cost);
    }
  }
  return bySeq;
}

std::string firstSequence(const std::string& analyzeOut) {
  std::istringstream iss(analyzeOut);
  std::string line;
  bool headerSeen = false;
  while (std::getline(iss, line)) {
    if (line.empty()) continue;
    if (!headerSeen) {
      headerSeen = true;
      continue;
    }
    std::istringstream row(line);
    std::string seq;
    if (row >> seq) return seq;
  }
  return "";
}
}  // namespace

TEST(CountPenaltyFFITest, FFIOverridesAffectMotionCountedCost) {
  void* lib = openVimficiencyLib();
  ASSERT_NE(lib, nullptr) << dlerror();

  using GetConfigFn = VimficiencyConfigFFI* (*)();
  using ApplyConfigFn = void (*)();
  using AnalyzeFn = const char* (*)(const char*, const char*, int, int, bool, bool,
                                    int, int, int, int, const char*, int, int, int);

  auto getConfig = reinterpret_cast<GetConfigFn>(dlsym(lib, "vimficiency_get_config"));
  auto applyConfig = reinterpret_cast<ApplyConfigFn>(dlsym(lib, "vimficiency_apply_config"));
  auto analyze = reinterpret_cast<AnalyzeFn>(dlsym(lib, "vimficiency_analyze"));
  ASSERT_NE(getConfig, nullptr);
  ASSERT_NE(applyConfig, nullptr);
  ASSERT_NE(analyze, nullptr);

  VimficiencyConfigFFI* cfg = getConfig();
  ASSERT_NE(cfg, nullptr);
  VimficiencyConfigFFI saved = *cfg;

  auto resetConfig = [&]() {
    *cfg = saved;
    applyConfig();
  };

  // Ensure clean baseline
  cfg->use_count_penalty_overrides = false;
  for (size_t i = 0; i < CountClassCOUNT; i++) {
    cfg->count_penalty_overrides[i] = {};
  }
  applyConfig();

  const char* initial = "one two three four five six";
  const char* goal = initial;

  std::string baselineOut = analyze(
      initial, goal,
      0, 26, false, false,
      0, 0, 0, 19,
      "", 40, 20, 20);
  auto baseline = parseBySequence(baselineOut);
  std::string baselineTop = firstSequence(baselineOut);

  auto it4w = baseline.find("4w");
  auto it4W = baseline.find("4W");
  ASSERT_TRUE(it4w != baseline.end() || it4W != baseline.end())
      << "Expected baseline to include 4w/4W\n" << baselineOut;

  // Apply strong overrides through FFI struct
  cfg->use_count_penalty_overrides = true;
  for (size_t i = 0; i < CountClassCOUNT; i++) {
    cfg->count_penalty_overrides[i] = {};
  }
  C_CountPenaltyOverride o{};
  o.has_base = true;
  o.base = 60.0;
  cfg->count_penalty_overrides[toIndex(CountClass::MotionWord)] = o;
  cfg->count_penalty_overrides[toIndex(CountClass::MotionWORD)] = o;
  applyConfig();

  std::string overriddenOut = analyze(
      initial, goal,
      0, 26, false, false,
      0, 0, 0, 19,
      "", 40, 20, 20);
  auto overridden = parseBySequence(overriddenOut);
  std::string overriddenTop = firstSequence(overriddenOut);

  if (it4w != baseline.end() && overridden.contains("4w")) {
    EXPECT_GT(overridden["4w"], it4w->second + 40.0);
  }
  if (it4W != baseline.end() && overridden.contains("4W")) {
    EXPECT_GT(overridden["4W"], it4W->second + 40.0);
  }
  EXPECT_NE(overriddenTop, "");
  EXPECT_NE(baselineTop, "");

  resetConfig();
  dlclose(lib);
}
