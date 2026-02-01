// tests/Utils/SeedManager.cpp

#include "SeedManager.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

SeedManager& SeedManager::instance() {
  static SeedManager instance;
  return instance;
}

SeedManager::SeedManager() {
  // Default seed file location (relative to where tests are run)
  seedFilePath_ = "tests/.last_seeds.txt";
  initFromEnvironment();
}

void SeedManager::initFromEnvironment() {
  // Check environment variable for mode
  const char* envMode = std::getenv("VIMFICIENCY_SEED_MODE");
  if (envMode) {
    std::string mode(envMode);
    if (mode == "random" || mode == "1") {
      mode_ = Mode::Random;
    } else if (mode == "replay") {
      mode_ = Mode::Replay;
    } else {
      mode_ = Mode::Fixed;
    }
  }

  // Legacy support: VIMFICIENCY_RANDOM_SEEDS=1
  const char* envRandom = std::getenv("VIMFICIENCY_RANDOM_SEEDS");
  if (envRandom && std::string(envRandom) == "1") {
    mode_ = Mode::Random;
  }

  // Check for custom seed file path
  const char* envSeedFile = std::getenv("VIMFICIENCY_SEED_FILE");
  if (envSeedFile) {
    seedFilePath_ = envSeedFile;
  }
}

void SeedManager::setMode(Mode mode) {
  if (mode_ != mode) {
    mode_ = mode;
    resetSeeds();

    if (mode_ == Mode::Replay) {
      loadSeedsFromFile();
    }
  }
}

void SeedManager::resetSeeds() {
  cachedSeeds_.clear();
  seedsLoaded_ = false;
}

int SeedManager::getSeed(int index) {
  switch (mode_) {
    case Mode::Fixed:
      return baseFixedSeed_ + index;

    case Mode::Random: {
      // Ensure we have enough cached seeds
      while (static_cast<int>(cachedSeeds_.size()) <= index) {
        int seed = generateRandomSeed();
        cachedSeeds_.push_back(seed);
        saveSeedsToFile();
      }
      return cachedSeeds_[index];
    }

    case Mode::Replay: {
      if (!seedsLoaded_) {
        loadSeedsFromFile();
      }
      if (index < static_cast<int>(replaySeeds_.size())) {
        return replaySeeds_[index];
      }
      // Fallback to fixed if replay file doesn't have enough seeds
      std::cerr << "Warning: Seed index " << index
                << " not found in replay file, using fixed seed\n";
      return baseFixedSeed_ + index;
    }
  }
  return baseFixedSeed_ + index;  // Unreachable
}

std::vector<int> SeedManager::getSeeds(int count) {
  std::vector<int> seeds;
  seeds.reserve(count);
  for (int i = 0; i < count; i++) {
    seeds.push_back(getSeed(i));
  }
  return seeds;
}

int SeedManager::generateRandomSeed() {
  // Use high-resolution clock + random_device for good entropy
  auto now = std::chrono::high_resolution_clock::now();
  auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
      now.time_since_epoch()).count();

  std::random_device rd;
  std::mt19937 gen(rd() ^ static_cast<unsigned>(nanos));
  std::uniform_int_distribution<int> dist(1, 1000000);

  return dist(gen);
}

void SeedManager::loadSeedsFromFile() {
  replaySeeds_.clear();
  seedsLoaded_ = true;

  std::ifstream file(seedFilePath_);
  if (!file.is_open()) {
    std::cerr << "Warning: Could not open seed file for replay: "
              << seedFilePath_ << "\n";
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    // Skip comments and empty lines
    if (line.empty() || line[0] == '#') continue;

    // Parse seed value
    try {
      int seed = std::stoi(line);
      replaySeeds_.push_back(seed);
    } catch (...) {
      // Skip invalid lines
    }
  }

  if (!replaySeeds_.empty()) {
    std::cerr << "[SeedManager] Loaded " << replaySeeds_.size()
              << " seeds from " << seedFilePath_ << "\n";
  }
}

void SeedManager::saveSeedsToFile() {
  std::ofstream file(seedFilePath_);
  if (!file.is_open()) {
    std::cerr << "Warning: Could not open seed file for writing: "
              << seedFilePath_ << "\n";
    return;
  }

  // Write header
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  file << "# Vimficiency test seeds\n";
  file << "# Generated: " << std::ctime(&time);
  file << "# Mode: random\n";
  file << "# To replay: VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_tests\n";
  file << "#\n";

  // Write seeds
  for (int seed : cachedSeeds_) {
    file << seed << "\n";
  }
}
