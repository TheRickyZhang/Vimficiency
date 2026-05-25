// tests/Utils/SeedManager.h
//
// Manages random seeds for reproducible testing.
// Default is random mode - generates seeds and logs them for reproducibility.
//
// Modes:
// - Random (default): generates random seeds, logs to tests/.last_seeds.txt
// - Fixed: uses deterministic seeds (42, 43, 44, ...) for debugging
// - Replay: reads seeds from file to reproduce a previous run
//
// Configuration (in test setup code):
//   SeedManager::instance().setFixedMode();   // Use fixed seeds for debugging
//   SeedManager::instance().setReplayMode();  // Replay from file
//
// Or via environment variable (for CI/scripts):
//   VIMFY_SEED_MODE=fixed   ./build/tests/vimfy_unit_tests
//   VIMFY_SEED_MODE=replay  ./build/tests/vimfy_unit_tests

#pragma once

#include <string>
#include <vector>

class SeedManager {
public:
  // Seed modes
  enum class Mode {
    Random,  // Generate random seeds and log to file (default)
    Fixed,   // Use deterministic seeds (42, 43, 44, ...)
    Replay   // Read seeds from file (for reproducing failures)
  };

  // Get the singleton instance
  static SeedManager& instance();

  // Mode control
  void setMode(Mode mode);
  Mode getMode() const { return mode_; }

  // Convenience setters for common modes
  void setFixedMode(int baseSeed = 42) {
    baseFixedSeed_ = baseSeed;
    setMode(Mode::Fixed);
  }
  void setReplayMode(const std::string& path = "") {
    if (!path.empty()) seedFilePath_ = path;
    setMode(Mode::Replay);
  }
  void setRandomMode() { setMode(Mode::Random); }

  // Get seed for a given index (0-based)
  // In Random mode: generates and logs a new seed (or returns cached)
  // In Fixed mode: returns baseFixedSeed + index
  // In Replay mode: returns seed from file
  int getSeed(int index);

  // Get multiple seeds at once
  std::vector<int> getSeeds(int count);

  // Reset cached seeds (call at start of new test suite if needed)
  void resetSeeds();

  // File path for seed log (default: tests/.last_seeds.txt)
  void setSeedFilePath(const std::string& path) { seedFilePath_ = path; }
  std::string getSeedFilePath() const { return seedFilePath_; }

  // Base seed for fixed mode (default: 42)
  void setBaseFixedSeed(int seed) { baseFixedSeed_ = seed; }
  int getBaseFixedSeed() const { return baseFixedSeed_; }

  // Check mode (for display purposes)
  bool isRandom() const { return mode_ == Mode::Random; }
  bool isFixed() const { return mode_ == Mode::Fixed; }
  bool isReplay() const { return mode_ == Mode::Replay; }

private:
  SeedManager();
  ~SeedManager() = default;

  // Non-copyable
  SeedManager(const SeedManager&) = delete;
  SeedManager& operator=(const SeedManager&) = delete;

  void initFromEnvironment();
  void loadSeedsFromFile();
  void saveSeedsToFile();
  int generateRandomSeed();

  Mode mode_ = Mode::Random;  // Default to random
  int baseFixedSeed_ = 42;
  std::string seedFilePath_;
  std::vector<int> cachedSeeds_;
  std::vector<int> replaySeeds_;
  bool seedsLoaded_ = false;
};

// Convenience macro for tests
#define TEST_SEED(index) SeedManager::instance().getSeed(index)
