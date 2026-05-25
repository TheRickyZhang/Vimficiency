#include "OracleReplay.h"

#include <exception>
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

void expectMatchesOracle(
    NeovimOracle& oracle,
    const Lines& initial,
    CursorPos initialPos,
    string_view sequence,
    const Lines& expectedLines,
    CursorPos expectedPos,
    Mode expectedMode,
    string_view context) {
  string seq(sequence);
  SCOPED_TRACE(::testing::Message()
               << "seq='" << seq << "'"
               << " from " << initialPos
               << contextText(context)
               << "\ninitial=" << initial);

  SimulationResult nvim;
  try {
    nvim = oracle.simulate(initial, initialPos.line, initialPos.col, seq);
  } catch (const exception& e) {
    oracle.restart();
    FAIL() << "NeovimOracle failed for seq='" << seq << "': " << e.what();
  }

  EXPECT_EQ(nvim.lines, expectedLines);
  EXPECT_EQ(nvim.row, expectedPos.line);
  EXPECT_EQ(nvim.col, expectedPos.col);
  EXPECT_EQ(nvim.mode, expectedMode)
      << "expected mode " << modeName(expectedMode)
      << ", got " << modeName(nvim.mode);
}

}  // namespace OracleReplay
