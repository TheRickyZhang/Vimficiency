#include "OracleReplay.h"

#include <string>

using namespace std;

namespace {

const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::Normal: return "Normal";
    case Mode::Insert: return "Insert";
    case Mode::Visual: return "Visual";
  }
  return "Unknown";
}

string contextText(string_view context) {
  return context.empty() ? "" : " (" + string(context) + ")";
}

}  // namespace

namespace OracleReplay {

::testing::AssertionResult matches(
    NeovimOracle& oracle,
    const Lines& initial,
    CursorPos initialPos,
    string_view sequence,
    const Lines& goal,
    optional<CursorPos> goalPos,
    optional<Mode> goalMode,
    string_view context) {
  string seq(sequence);
  SimulationResult nvim = oracle.simulate(initial, initialPos.line, initialPos.col, seq);

  bool ok = nvim.lines == goal;
  if (goalPos) {
    ok = ok && nvim.row == goalPos->line && nvim.col == goalPos->col;
  }
  if (goalMode) {
    ok = ok && nvim.mode == *goalMode;
  }
  if (ok) return ::testing::AssertionSuccess();

  auto failure = ::testing::AssertionFailure()
      << "Replay mismatch" << contextText(context)
      << " seq='" << seq << "' from " << initialPos
      << "\n  Initial: " << initial
      << "\n  Goal:    " << goal
      << "\n  Got:     " << nvim.lines;

  if (goalPos) {
    failure << "\n  Goal cursor: " << *goalPos
            << "\n  Got cursor:  (" << nvim.row << ", " << nvim.col << ")";
  }
  if (goalMode) {
    failure << "\n  Goal mode: " << modeName(*goalMode)
            << "\n  Got mode:  " << modeName(nvim.mode);
  }

  return failure;
}

}  // namespace OracleReplay
