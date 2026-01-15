#pragma once

#include "Editor/Mode.h"
#include "Utils/Lines.h"

#include <memory>
#include <string>
#include <vector>

struct SimulationResult {
  Lines lines;
  int row; // 0-indexed
  int col; // 0-indexed
  Mode mode;
};

/*
 * Manages a persistent Neovim subprocess for testing Vim motion/edit behavior.
 * Uses msgpack-RPC over stdin/stdout to communicate with nvim --embed.
 *
 * Usage:
 *   class MyTest : public ::testing::Test {
 *   protected:
 *       static void SetUpTestSuite() { oracle_ =
 * std::make_unique<NeovimOracle>(); } static void TearDownTestSuite() {
 * oracle_.reset(); } static std::unique_ptr<NeovimOracle> oracle_;
 *   };
 *   std::unique_ptr<NeovimOracle> MyTest::oracle_;
 */

class NeovimOracle {
public:
  NeovimOracle();
  ~NeovimOracle();

  // Non-copyable, non-movable (owns subprocess)
  NeovimOracle(const NeovimOracle &) = delete;
  NeovimOracle &operator=(const NeovimOracle &) = delete;

  // Simulate executing a key sequence in Neovim starting from normal mode.
  // Returns the resulting buffer state, cursor position, and mode.
  SimulationResult simulate(const Lines &lines,
                            int startRow, // 0-indexed
                            int startCol, // 0-indexed
                            const std::string &keys);

  // Restart the Neovim subprocess (call periodically to prevent buffer exhaustion)
  void restart();

private:
  // Pimpl is cleaner, given the subprocess has messy details
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
