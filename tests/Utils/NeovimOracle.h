#pragma once

#include "Types/Mode.h"
#include "Types/Lines.h"

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
 * Auto-restarts every AUTO_RESTART_INTERVAL simulate() calls to prevent
 * buffer exhaustion. Each simulate() call is self-contained (creates and
 * deletes its own buffer), so restarts are transparent to callers.
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

  // Simulate parsed Normal-mode tokens separately in one Neovim buffer.
  SimulationResult simulateTokens(const Lines &lines,
                                  int startRow,
                                  int startCol,
                                  const std::vector<std::string>& tokens);

  // Specialized entry point for NavContext-dependent scroll motions
  // (<C-d>/<C-u> via 'scroll', <C-f>/<C-b> via window height). Sets window-local
  // 'scroll' (clamped to the live window height) for this one call; the general
  // simulate() path is untouched. Headless nvim can't resize its sole window,
  // so window height is fixed — query it via windowHeight() and mirror it into
  // the model side's NavContext rather than trying to vary it here.
  SimulationResult simulateScroll(const Lines &lines,
                                  int startRow,
                                  int startCol,
                                  const std::string &keys,
                                  int scrollAmount);

  // The live (fixed) window height of the headless session.
  int windowHeight();

  // Restart the Neovim subprocess (resets call counter)
  void restart();

private:
  // Pimpl is cleaner, given the subprocess has messy details
  struct Impl;
  std::unique_ptr<Impl> impl_;

  // Auto-restart after this many simulate() calls to prevent buffer exhaustion
  static constexpr int AUTO_RESTART_INTERVAL = 200;
  int callsSinceRestart_ = 0;

  // scrollAmount < 0 leaves 'scroll' untouched (the default for every caller
  // except simulateScroll).
  SimulationResult simulateChunks(const Lines& lines,
                                  int startRow,
                                  int startCol,
                                  const std::vector<std::string>& chunks,
                                  bool asSeparateUserActions,
                                  int scrollAmount = -1);
};
