#include <gtest/gtest.h>

#include <dlfcn.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Keyboard/Key.h"
#include "LuaExports/Api.h"
#include "Optimizer/CountPenalty.h"

namespace {
void* openVimfyLib() {
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

// Analyze's machine-readable body line is `<raw_seq>\x1f<cost>\n`, using
// `kEventFieldSep` (0x1F) — not whitespace — so that insert-mode text
// and other unusual bytes survive round-tripping. See
// `dev/lua/ffi-separators.md` and `AnalyzeExports.cpp`. The previous
// version of this parser used `iss >> seq >> cost`, which silently
// failed after the separator was switched from space to 0x1F because
// `operator>>` stops on whitespace only.
constexpr char kFieldSep = '\x1f';

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
    const auto sepPos = line.find(kFieldSep);
    if (sepPos == std::string::npos) continue;
    const std::string seq = line.substr(0, sepPos);
    const std::string costStr = line.substr(sepPos + 1);
    try {
      const double cost = std::stod(costStr);
      bySeq.emplace(seq, cost);
    } catch (...) {
      // Malformed cost — skip. A stray DEBUG trailer in a tracking-enabled
      // build could land here; the test only cares about body rows.
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
    const auto sepPos = line.find(kFieldSep);
    if (sepPos == std::string::npos) continue;
    return line.substr(0, sepPos);
  }
  return "";
}

std::string encodeLineArray(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& line : lines) {
    out += std::to_string(line.size());
    out += ':';
    out += line;
  }
  return out;
}
}  // namespace

TEST(CountPenaltyFFITest, FFIOverridesAffectMotionCountedCost) {
  void* lib = openVimfyLib();
  ASSERT_NE(lib, nullptr) << dlerror();

  using GetConfigFn = VFConfig* (*)();
  using ApplyConfigFn = void (*)();
  using AnalyzeFn = VFByteSlice (*)(
      const char*, size_t, const char*, size_t,
      int, int, bool, bool,
      int, int, int, int,
      const char*, size_t,
      int, int, int,
      const char*, size_t);

  auto getConfig = reinterpret_cast<GetConfigFn>(dlsym(lib, "vf_get_config"));
  auto applyConfig = reinterpret_cast<ApplyConfigFn>(dlsym(lib, "vf_apply_config"));
  auto analyze = reinterpret_cast<AnalyzeFn>(dlsym(lib, "vf_analyze"));
  ASSERT_NE(getConfig, nullptr);
  ASSERT_NE(applyConfig, nullptr);
  ASSERT_NE(analyze, nullptr);

  VFConfig* cfg = getConfig();
  ASSERT_NE(cfg, nullptr);
  VFConfig saved = *cfg;

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

  const std::string initial = encodeLineArray({"one two three four five six"});
  const std::string goal = initial;

  VFByteSlice baselineSlice = analyze(
      initial.data(), initial.size(),
      goal.data(), goal.size(),
      0, 26, false, false,
      0, 0, 0, 19,
      "", 0,
      40, 20, 20,
      "", 0);
  std::string baselineOut(baselineSlice.data, baselineSlice.len);
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
  VFCountPenaltyOverride o{};
  o.has_base = true;
  o.base = 60.0;
  cfg->count_penalty_overrides[toIndex(CountClass::MovementWord)] = o;
  cfg->count_penalty_overrides[toIndex(CountClass::MovementBigWord)] = o;
  applyConfig();

  VFByteSlice overriddenSlice = analyze(
      initial.data(), initial.size(),
      goal.data(), goal.size(),
      0, 26, false, false,
      0, 0, 0, 19,
      "", 0,
      40, 20, 20,
      "", 0);
  std::string overriddenOut(overriddenSlice.data, overriddenSlice.len);
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
