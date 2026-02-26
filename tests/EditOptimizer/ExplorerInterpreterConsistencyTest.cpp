// tests/EditOptimizer/ExplorerInterpreterConsistencyTest.cpp
//
// Consistency test: verifies that the A* explorer (EditSearchContext + EditState)
// and the interpreter (Edit::applyEdit) produce identical results for every edit
// operation at every cursor position.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="ExplorerInterpreterConsistency*"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "Boundary/EditBoundary.h"
#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MotionInterpreter.h"
#include "Keyboard/Config.h"
#include "Optimizer/EditOptimizer/EditSearchContext.h"
#include "Optimizer/EditOptimizer/EditState.h"
#include "Optimizer/EditOptimizer/EditOptimizerParams.h"
#include "Optimizer/SequenceBinding.h"
#include "Types/Lines.h"
#include "Types/CursorPos.h"
#include "Types/LineRange.h"
#include "Types/Mode.h"
#include "Types/NavContext.h"
#include "Types/Range.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimMotionUtils.h"

using namespace std;

// =============================================================================
// Mismatch recording
// =============================================================================

struct Mismatch {
  string command;
  CursorPos startPos;
  Lines initialLines;
  // Explorer result
  Lines explorerLines;
  CursorPos explorerPos;
  // Interpreter result
  Lines interpreterLines;
  CursorPos interpreterPos;
};

static string formatLines(const Lines& lines) {
  ostringstream oss;
  for (int i = 0; i < static_cast<int>(lines.size()); i++) {
    oss << "  [" << i << "] \"" << lines[i] << "\"\n";
  }
  return oss.str();
}

static string formatMismatch(const Mismatch& m) {
  ostringstream oss;
  oss << "Command: " << m.command
      << " at (" << m.startPos.line << "," << m.startPos.col << ")\n";
  oss << "Initial buffer:\n" << formatLines(m.initialLines);
  oss << "Explorer  -> pos=(" << m.explorerPos.line << "," << m.explorerPos.col << ")\n"
      << formatLines(m.explorerLines);
  oss << "Interpreter -> pos=(" << m.interpreterPos.line << "," << m.interpreterPos.col << ")\n"
      << formatLines(m.interpreterLines);
  return oss.str();
}

struct MotionMismatch {
  string motion;
  CursorPos startPos;
  Lines lines;
  CursorPos explorerPos;
  CursorPos interpreterPos;
};

static string formatMotionMismatch(const MotionMismatch& m) {
  ostringstream oss;
  oss << "Motion: " << m.motion
      << " at (" << m.startPos.line << "," << m.startPos.col << ")\n";
  oss << "Buffer:\n" << formatLines(m.lines);
  oss << "Explorer  -> (" << m.explorerPos.line << "," << m.explorerPos.col << ")\n";
  oss << "Interpreter -> (" << m.interpreterPos.line << "," << m.interpreterPos.col << ")\n";
  return oss.str();
}

// =============================================================================
// Command string reconstruction from SequenceBinding
// =============================================================================

static string commandString(const SequenceBinding& sb) {
  string cmd;
  if (sb.count > 0) cmd = to_string(sb.count);
  cmd += string(sb.base.seq.view());
  return cmd;
}

// =============================================================================
// Test fixture
// =============================================================================

class ExplorerInterpreterConsistency : public ::testing::Test {
protected:
  Config config = Config::uniform();
  EditOptimizerParams params = EditOptimizerParams().withMaxResults(1);

  // Run interpreter on a fresh copy and return (lines, pos)
  pair<Lines, CursorPos> runInterpreter(const Lines& effLines, CursorPos cursor,
                                         const string& cmd,
                                         bool hasLinesBelow, int leftColOffset,
                                         int rightColOffset, bool hasLinesAbove) {
    Lines lines = effLines;
    Mode mode = Mode::Normal;
    auto edits = Edit::parseEdits(cmd);
    for (auto& e : edits) {
      Edit::applyEdit(lines, cursor, mode, e, nullptr,
                      hasLinesBelow, leftColOffset, rightColOffset, hasLinesAbove);
    }
    return {lines, cursor};
  }

  // Core: check all positions in effective lines for explorer/interpreter agreement
  void checkAllPositions(const Lines& initialLines, const EditBoundary& boundary) {
    EditSearchContext ctx(initialLines, boundary, params, config);
    const Lines& effLines = ctx.effectiveLines;
    int leftOff = ctx.leftColOffset;
    int rightOff = ctx.rightColOffset;
    bool hasBelow = boundary.hasLinesBelow();
    bool hasAbove = boundary.hasLinesAbove();

    vector<Mismatch> mismatches;
    vector<MotionMismatch> motionMismatches;

    for (int row = 0; row < static_cast<int>(effLines.size()); row++) {
      int cols = effLines[row].effectiveSize();
      for (int col = 0; col < cols; col++) {
        CursorPos cursor(row, col);
        if (ctx.inBoundaryRegion(cursor, effLines)) continue;

        EditState state(effLines, cursor, 0, 0.0);

        auto compare = [&](const Lines& explorerLines, CursorPos explorerPos,
                           const string& cmd) {
          auto [intLines, intPos] = runInterpreter(effLines, cursor, cmd,
                                                    hasBelow, leftOff, rightOff, hasAbove);
          if (explorerLines != intLines || explorerPos != intPos) {
            mismatches.push_back({cmd, cursor, effLines,
                                  explorerLines, explorerPos,
                                  intLines, intPos});
          }
        };

        // --- Characterwise deletions ---
        DeletionCallback onDeletion = [&](const Range& range, const SequenceBinding& sb) {
          EditState after = state.afterDeletion(range);
          compare(after.getLines(), after.getPos(), commandString(sb));
        };

        // --- Linewise deletion (dd) ---
        LinewiseCallback onLinewise = [&](int line, const SequenceBinding& sb) {
          EditState after = state.afterLinewiseDeletion(line, hasBelow);
          CursorPos afterPos = after.getPos();
          // Skip OOB states (the real A* only uses these at goal)
          if (afterPos.line < 0 ||
              afterPos.line >= static_cast<int>(after.getLines().size())) return;
          compare(after.getLines(), afterPos, commandString(sb));
        };

        // --- Join (J/gJ) ---
        JoinCallback onJoin = [&](bool addSpace, const SequenceBinding& sb) {
          EditState after = state.afterJoin(addSpace);
          compare(after.getLines(), after.getPos(), commandString(sb));
        };

        ctx.exploreAllDeletions(state, onDeletion, onLinewise, onJoin);

        // --- Counted word edits ---
        ctx.exploreCountedWordEdits(state, onDeletion);

        // --- Counted char edits ---
        ctx.exploreCountedCharEdits(state, onDeletion);

        // --- Counted linewise edits (dj, dk, {n}dd) ---
        CountedLinewiseCallback onCountedLinewise = [&](LineRange range, const SequenceBinding& sb) {
          EditState after = state.afterMultiLinewiseDeletion(range, hasBelow);
          CursorPos afterPos = after.getPos();
          if (afterPos.line < 0 ||
              afterPos.line >= static_cast<int>(after.getLines().size())) return;
          compare(after.getLines(), afterPos, commandString(sb));
        };
        ctx.exploreCountedLineEdits(state, onCountedLinewise);

        // --- Counted join ({n}J, {n}gJ) ---
        CountedJoinCallback onCountedJoin = [&](bool addSpace, const SequenceBinding& sb) {
          EditState after = state.afterMultiJoin(sb.count, addSpace);
          compare(after.getLines(), after.getPos(), commandString(sb));
        };
        ctx.exploreCountedJoinCommands(state, onCountedJoin);

        // --- Motion consistency ---
        // Compare VimCore motion functions (explorer path) vs simulateMotions (interpreter path)
        NavContext navCtx;

        struct MotionSpec {
          const char* cmd;
          function<void(CursorPos&, const Lines&)> vimCoreFn;
        };
        vector<MotionSpec> motions = {
          {"w",  [](CursorPos& p, const Lines& l) { VimCore::motionW(p, l, false); p.setCol(VimCore::clampCol(l, p.col, p.line)); }},
          {"W",  [](CursorPos& p, const Lines& l) { VimCore::motionW(p, l, true); p.setCol(VimCore::clampCol(l, p.col, p.line)); }},
          {"b",  [](CursorPos& p, const Lines& l) { VimCore::motionB(p, l, false); }},
          {"B",  [](CursorPos& p, const Lines& l) { VimCore::motionB(p, l, true); }},
          {"e",  [](CursorPos& p, const Lines& l) { VimCore::motionE(p, l, false); p.setCol(VimCore::clampCol(l, p.col, p.line)); }},
          {"E",  [](CursorPos& p, const Lines& l) { VimCore::motionE(p, l, true); p.setCol(VimCore::clampCol(l, p.col, p.line)); }},
          {"ge", [](CursorPos& p, const Lines& l) { VimCore::motionGe(p, l, false); }},
          {"gE", [](CursorPos& p, const Lines& l) { VimCore::motionGe(p, l, true); }},
          {")",  [](CursorPos& p, const Lines& l) { VimCore::motionSentenceNext(p, l); }},
          {"(",  [](CursorPos& p, const Lines& l) { VimCore::motionSentencePrev(p, l); }},
          {"}",  [](CursorPos& p, const Lines& l) { VimCore::motionParagraphNext(p, l); }},
          {"{",  [](CursorPos& p, const Lines& l) { VimCore::motionParagraphPrev(p, l); }},
          {"$",  [](CursorPos& p, const Lines& l) {
            int len = static_cast<int>(l[p.line].size());
            p.col = len == 0 ? 0 : len - 1;
            p.targetCol = TARGETCOL_EOL;
          }},
          {"0",  [](CursorPos& p, const Lines&) { p.setCol(0); }},
          {"^",  [](CursorPos& p, const Lines& l) {
            int len = static_cast<int>(l[p.line].size());
            int c = 0;
            while (c < len && isspace(static_cast<unsigned char>(l[p.line][c]))) ++c;
            if (c >= len && len > 0) c = len - 1;
            p.setCol(c);
          }},
          {"h",  [](CursorPos& p, const Lines& l) { VimCore::moveCol(p, l, -1); }},
          {"l",  [](CursorPos& p, const Lines& l) { VimCore::moveCol(p, l, 1); }},
          {"j",  [](CursorPos& p, const Lines& l) { VimCore::moveLine(p, l, 1); }},
          {"k",  [](CursorPos& p, const Lines& l) { VimCore::moveLine(p, l, -1); }},
        };

        for (const auto& m : motions) {
          CursorPos explorerPos = cursor;
          m.vimCoreFn(explorerPos, effLines);
          CursorPos interpPos = simulateMotions(cursor, m.cmd, effLines, navCtx);
          if (explorerPos.line != interpPos.line || explorerPos.col != interpPos.col) {
            motionMismatches.push_back({m.cmd, cursor, effLines, explorerPos, interpPos});
          }
        }
      }
    }

    // Report edit mismatches
    if (!mismatches.empty()) {
      ostringstream report;
      report << mismatches.size() << " edit mismatch(es) found:\n\n";
      int shown = min(static_cast<int>(mismatches.size()), 10);
      for (int i = 0; i < shown; i++) {
        report << "--- Mismatch " << (i + 1) << " ---\n"
               << formatMismatch(mismatches[i]) << "\n";
      }
      if (static_cast<int>(mismatches.size()) > shown) {
        report << "... and " << (mismatches.size() - shown) << " more\n";
      }
      FAIL() << report.str();
    }

    // Report motion mismatches
    if (!motionMismatches.empty()) {
      ostringstream report;
      report << motionMismatches.size() << " motion mismatch(es) found:\n\n";
      int shown = min(static_cast<int>(motionMismatches.size()), 10);
      for (int i = 0; i < shown; i++) {
        report << "--- Motion Mismatch " << (i + 1) << " ---\n"
               << formatMotionMismatch(motionMismatches[i]) << "\n";
      }
      if (static_cast<int>(motionMismatches.size()) > shown) {
        report << "... and " << (motionMismatches.size() - shown) << " more\n";
      }
      FAIL() << report.str();
    }
  }
};

// =============================================================================
// Test cases
// =============================================================================

TEST_F(ExplorerInterpreterConsistency, NoBoundary_RandomLines) {
  constexpr int ITERATIONS = 20;
  unsigned baseSeed = testSeed();

  for (int i = 0; i < ITERATIONS; i++) {
    RandomGen::seed(baseSeed + i);
    Lines lines = randomLines(RandomGen::range(3, 5), 5, 20);
    EditBoundary boundary(lines, {0, 0}, lines.endPos());
    SCOPED_TRACE("iter=" + to_string(i) + " seed=" + to_string(baseSeed + i));
    checkAllPositions(lines, boundary);
  }
}

TEST_F(ExplorerInterpreterConsistency, NoBoundary_WithEmptyLines) {
  constexpr int ITERATIONS = 20;
  unsigned baseSeed = testSeed();

  for (int i = 0; i < ITERATIONS; i++) {
    RandomGen::seed(baseSeed + i);
    // Generate lines interspersed with empty lines
    Lines lines;
    int numLines = RandomGen::range(3, 6);
    for (int j = 0; j < numLines; j++) {
      if (RandomGen::chance(1, 3)) {
        lines.push_back("");
      } else {
        lines.push_back(randomLine(RandomGen::range(5, 20)));
      }
    }
    // Ensure at least one non-empty line
    if (lines.empty() || (lines.size() == 1 && lines[0].empty())) {
      lines.push_back(randomLine(5));
    }
    EditBoundary boundary(lines, {0, 0}, lines.endPos());
    SCOPED_TRACE("iter=" + to_string(i) + " seed=" + to_string(baseSeed + i));
    checkAllPositions(lines, boundary);
  }
}

TEST_F(ExplorerInterpreterConsistency, NoBoundary_ProseBuffer) {
  constexpr int ITERATIONS = 20;
  unsigned baseSeed = testSeed();

  for (int i = 0; i < ITERATIONS; i++) {
    RandomGen::seed(baseSeed + i);
    Lines lines = randomProseBuffer(4);
    EditBoundary boundary(lines, {0, 0}, lines.endPos());
    SCOPED_TRACE("iter=" + to_string(i) + " seed=" + to_string(baseSeed + i));
    checkAllPositions(lines, boundary);
  }
}

TEST_F(ExplorerInterpreterConsistency, NoBoundary_SingleLine) {
  constexpr int ITERATIONS = 20;
  unsigned baseSeed = testSeed();

  for (int i = 0; i < ITERATIONS; i++) {
    RandomGen::seed(baseSeed + i);
    Lines lines = {randomLine(RandomGen::range(10, 30))};
    EditBoundary boundary(lines, {0, 0}, lines.endPos());
    SCOPED_TRACE("iter=" + to_string(i) + " seed=" + to_string(baseSeed + i));
    checkAllPositions(lines, boundary);
  }
}

TEST_F(ExplorerInterpreterConsistency, WithBoundary_Embedded) {
  constexpr int ITERATIONS = 20;
  unsigned baseSeed = testSeed();

  for (int i = 0; i < ITERATIONS; i++) {
    RandomGen::seed(baseSeed + i);
    auto region = generateRandomMultiLineEmbedded();
    EditBoundary boundary = region.makeBoundary();
    SCOPED_TRACE("iter=" + to_string(i) + " seed=" + to_string(baseSeed + i));
    checkAllPositions(region.editRegion, boundary);
  }
}

TEST_F(ExplorerInterpreterConsistency, WithBoundary_LinesAboveBelow) {
  constexpr int ITERATIONS = 20;
  unsigned baseSeed = testSeed();

  for (int i = 0; i < ITERATIONS; i++) {
    RandomGen::seed(baseSeed + i);
    // Create a buffer with lines above and below simulated by embedding
    // in a larger context
    Lines editLines = randomLines(RandomGen::range(2, 4), 5, 15);

    // Build a full buffer with context lines
    Lines fullBuffer;
    fullBuffer.push_back(randomLine(RandomGen::range(5, 15)));  // line above
    for (auto& l : editLines) fullBuffer.push_back(l);
    fullBuffer.push_back(randomLine(RandomGen::range(5, 15)));  // line below

    int startLine = 1;
    int endLine = startLine + static_cast<int>(editLines.size()) - 1;
    CursorPos beginPos(startLine, 0);
    CursorPos endPos(endLine, fullBuffer[endLine].effectiveSize());

    EditBoundary boundary(fullBuffer, beginPos, endPos);
    SCOPED_TRACE("iter=" + to_string(i) + " seed=" + to_string(baseSeed + i));
    checkAllPositions(editLines, boundary);
  }
}

TEST_F(ExplorerInterpreterConsistency, NoBoundary_SentencePunctuation) {
  constexpr int ITERATIONS = 20;
  unsigned baseSeed = testSeed();

  for (int i = 0; i < ITERATIONS; i++) {
    RandomGen::seed(baseSeed + i);
    // Hand-crafted patterns with sentence-ending punctuation
    Lines lines;
    int numLines = RandomGen::range(2, 4);
    for (int j = 0; j < numLines; j++) {
      string line;
      // Build a line with 1-3 sentences
      int numSentences = RandomGen::range(1, 3);
      for (int s = 0; s < numSentences; s++) {
        if (s > 0) line += "  ";  // double space between sentences
        line += randomSentence();
      }
      lines.push_back(line);
    }
    EditBoundary boundary(lines, {0, 0}, lines.endPos());
    SCOPED_TRACE("iter=" + to_string(i) + " seed=" + to_string(baseSeed + i));
    checkAllPositions(lines, boundary);
  }
}

TEST_F(ExplorerInterpreterConsistency, NoBoundary_ParagraphBoundaries) {
  constexpr int ITERATIONS = 20;
  unsigned baseSeed = testSeed();

  for (int i = 0; i < ITERATIONS; i++) {
    RandomGen::seed(baseSeed + i);
    // Lines with blank-line paragraph breaks
    Lines lines;
    lines.push_back(randomLine(RandomGen::range(5, 15)));
    lines.push_back(randomLine(RandomGen::range(5, 15)));
    lines.push_back("");  // paragraph break
    lines.push_back(randomLine(RandomGen::range(5, 15)));
    lines.push_back(randomLine(RandomGen::range(5, 15)));
    EditBoundary boundary(lines, {0, 0}, lines.endPos());
    SCOPED_TRACE("iter=" + to_string(i) + " seed=" + to_string(baseSeed + i));
    checkAllPositions(lines, boundary);
  }
}

// =============================================================================
// Oracle verification: interpreter vs Neovim
// =============================================================================

class InterpreterOracleConsistency : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle_;
  static void SetUpTestSuite() { oracle_ = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle_.reset(); }

  // Verify motion commands: compare simulateMotions vs Neovim cursor position
  void verifyMotions(const Lines& lines, const vector<string>& motions,
                     int iterations, int numLines) {
    unsigned baseSeed = testSeed();
    int failures = 0;
    ostringstream report;

    for (int i = 0; i < iterations; i++) {
      RandomGen::seed(baseSeed + i);
      Lines buf = (numLines > 0) ? randomLines(RandomGen::range(2, numLines), 5, 20)
                                 : lines;
      CursorPos start = randomPosition(buf);

      for (const auto& cmd : motions) {
        CursorPos ours = simulateMotions(start, cmd, buf);
        auto result = oracle_->simulate(buf, start.line, start.col, cmd);
        CursorPos nvim(result.row, result.col);

        if (ours.line != nvim.line || ours.col != nvim.col) {
          if (failures < 10) {
            report << "Motion '" << cmd << "' at (" << start.line << "," << start.col
                   << ") ours=(" << ours.line << "," << ours.col
                   << ") nvim=(" << nvim.line << "," << nvim.col << ")\n"
                   << "Buffer:\n" << formatLines(buf) << "\n";
          }
          failures++;
        }
      }
    }

    if (failures > 0) {
      FAIL() << failures << " motion oracle mismatch(es):\n" << report.str();
    }
  }

  // Verify edit commands: compare applyEdit buffer vs Neovim buffer
  void verifyEdits(const Lines& lines, const vector<string>& cmds,
                   int iterations, int numLines) {
    unsigned baseSeed = testSeed();
    int failures = 0;
    ostringstream report;

    for (int i = 0; i < iterations; i++) {
      RandomGen::seed(baseSeed + i);
      Lines buf = (numLines > 0) ? randomLines(RandomGen::range(2, numLines), 5, 20)
                                 : lines;
      CursorPos start = randomPosition(buf);

      for (const auto& cmd : cmds) {
        // Our interpreter
        Lines ourLines = buf;
        CursorPos ourPos = start;
        Mode mode = Mode::Normal;
        auto edits = Edit::parseEdits(cmd);
        for (auto& e : edits) {
          Edit::applyEdit(ourLines, ourPos, mode, e);
        }

        // Neovim oracle
        auto result = oracle_->simulate(buf, start.line, start.col, cmd);

        // For change commands, Neovim will be in insert mode - press Esc
        // to get back to normal mode for buffer comparison
        string oracleCmd = cmd;
        bool isChange = false;
        // Detect change commands
        if (cmd[0] == 'c' || cmd == "s" || cmd == "S" || cmd == "C") {
          isChange = true;
        } else {
          // Check for counted change: strip digits then check
          size_t j = 0;
          while (j < cmd.size() && isdigit(cmd[j])) j++;
          if (j < cmd.size() && (cmd[j] == 'c' || cmd[j] == 's' || cmd[j] == 'S')) {
            isChange = true;
          }
        }
        if (isChange) {
          // Re-simulate with Esc appended to exit insert mode
          result = oracle_->simulate(buf, start.line, start.col, cmd + "\x1b");
        }

        bool linesMismatch = (ourLines != result.lines);
        bool posMismatch = (ourPos.line != result.row || ourPos.col != result.col);

        // For change commands, only compare buffer (cursor/mode differs)
        if (isChange) posMismatch = false;

        if (linesMismatch || posMismatch) {
          if (failures < 10) {
            report << "Edit '" << cmd << "' at (" << start.line << "," << start.col << ")\n"
                   << "Initial:\n" << formatLines(buf)
                   << "Ours -> pos=(" << ourPos.line << "," << ourPos.col << ")\n"
                   << formatLines(ourLines)
                   << "Nvim -> pos=(" << result.row << "," << result.col << ")\n"
                   << formatLines(result.lines) << "\n";
          }
          failures++;
        }
      }
    }

    if (failures > 0) {
      FAIL() << failures << " edit oracle mismatch(es):\n" << report.str();
    }
  }

  // Convenience: verify with prose buffers
  void verifyMotionsProse(const vector<string>& motions, int iterations) {
    unsigned baseSeed = testSeed();
    int failures = 0;
    ostringstream report;

    for (int i = 0; i < iterations; i++) {
      RandomGen::seed(baseSeed + i);
      Lines buf = randomProseBuffer(4);
      CursorPos start = randomPosition(buf);

      for (const auto& cmd : motions) {
        CursorPos ours = simulateMotions(start, cmd, buf);
        auto result = oracle_->simulate(buf, start.line, start.col, cmd);
        CursorPos nvim(result.row, result.col);

        if (ours.line != nvim.line || ours.col != nvim.col) {
          if (failures < 10) {
            report << "Motion '" << cmd << "' at (" << start.line << "," << start.col
                   << ") ours=(" << ours.line << "," << ours.col
                   << ") nvim=(" << nvim.line << "," << nvim.col << ")\n"
                   << "Buffer:\n" << formatLines(buf) << "\n";
          }
          failures++;
        }
      }
    }

    if (failures > 0) {
      FAIL() << failures << " motion oracle mismatch(es):\n" << report.str();
    }
  }

  void verifyEditsProse(const vector<string>& cmds, int iterations) {
    unsigned baseSeed = testSeed();
    int failures = 0;
    ostringstream report;

    for (int i = 0; i < iterations; i++) {
      RandomGen::seed(baseSeed + i);
      Lines buf = randomProseBuffer(4);
      CursorPos start = randomPosition(buf);

      for (const auto& cmd : cmds) {
        Lines ourLines = buf;
        CursorPos ourPos = start;
        Mode mode = Mode::Normal;
        auto edits = Edit::parseEdits(cmd);
        for (auto& e : edits) {
          Edit::applyEdit(ourLines, ourPos, mode, e);
        }

        auto result = oracle_->simulate(buf, start.line, start.col, cmd);

        if (ourLines != result.lines || ourPos.line != result.row || ourPos.col != result.col) {
          if (failures < 10) {
            report << "Edit '" << cmd << "' at (" << start.line << "," << start.col << ")\n"
                   << "Initial:\n" << formatLines(buf)
                   << "Ours -> pos=(" << ourPos.line << "," << ourPos.col << ")\n"
                   << formatLines(ourLines)
                   << "Nvim -> pos=(" << result.row << "," << result.col << ")\n"
                   << formatLines(result.lines) << "\n";
          }
          failures++;
        }
      }
    }

    if (failures > 0) {
      FAIL() << failures << " edit oracle mismatch(es):\n" << report.str();
    }
  }

  // Generate paragraph buffer (lines with blank-line breaks)
  Lines randomParagraphBuffer() {
    Lines lines;
    int numParas = RandomGen::range(2, 4);
    for (int p = 0; p < numParas; p++) {
      if (p > 0) lines.push_back("");
      int paraLines = RandomGen::range(1, 3);
      for (int l = 0; l < paraLines; l++) {
        lines.push_back(randomLine(RandomGen::range(5, 20)));
      }
    }
    return lines;
  }
};

unique_ptr<NeovimOracle> InterpreterOracleConsistency::oracle_;

// =============================================================================
// Oracle: Motion tests
// =============================================================================

TEST_F(InterpreterOracleConsistency, Oracle_WordMotions) {
  verifyMotions({}, {"w", "W", "b", "B", "e", "E", "ge", "gE"}, 20, 4);
}

TEST_F(InterpreterOracleConsistency, Oracle_SentenceMotions) {
  verifyMotionsProse({")", "("}, 20);
}

TEST_F(InterpreterOracleConsistency, Oracle_ParagraphMotions) {
  unsigned baseSeed = testSeed();
  int failures = 0;
  ostringstream report;

  for (int i = 0; i < 20; i++) {
    RandomGen::seed(baseSeed + i);
    Lines buf = randomParagraphBuffer();
    CursorPos start = randomPosition(buf);

    for (const auto& cmd : {"}", "{"}) {
      CursorPos ours = simulateMotions(start, cmd, buf);
      auto result = oracle_->simulate(buf, start.line, start.col, cmd);
      CursorPos nvim(result.row, result.col);

      if (ours.line != nvim.line || ours.col != nvim.col) {
        if (failures < 10) {
          report << "Motion '" << cmd << "' at (" << start.line << "," << start.col
                 << ") ours=(" << ours.line << "," << ours.col
                 << ") nvim=(" << nvim.line << "," << nvim.col << ")\n"
                 << "Buffer:\n" << formatLines(buf) << "\n";
        }
        failures++;
      }
    }
  }

  if (failures > 0) {
    FAIL() << failures << " paragraph motion oracle mismatch(es):\n" << report.str();
  }
}

TEST_F(InterpreterOracleConsistency, Oracle_LineMotions) {
  verifyMotions({}, {"0", "^", "$", "h", "l", "j", "k"}, 20, 4);
}

// =============================================================================
// Oracle: Delete tests
// =============================================================================

TEST_F(InterpreterOracleConsistency, Oracle_WordDeletes) {
  verifyEdits({}, {"de", "dE", "dw", "dW", "db", "dB", "dge", "dgE"}, 20, 4);
}

TEST_F(InterpreterOracleConsistency, Oracle_TextObjects) {
  verifyEdits({}, {"diw", "daw", "diW", "daW"}, 20, 4);
}

TEST_F(InterpreterOracleConsistency, Oracle_SentenceDeletes) {
  verifyEditsProse({"d)", "d("}, 20);
}

TEST_F(InterpreterOracleConsistency, Oracle_ParagraphDeletes) {
  unsigned baseSeed = testSeed();
  int failures = 0;
  ostringstream report;

  for (int i = 0; i < 20; i++) {
    RandomGen::seed(baseSeed + i);
    Lines buf = randomParagraphBuffer();
    CursorPos start = randomPosition(buf);

    for (const auto& cmd : {"d}", "d{"}) {
      Lines ourLines = buf;
      CursorPos ourPos = start;
      Mode mode = Mode::Normal;
      auto edits = Edit::parseEdits(cmd);
      for (auto& e : edits) {
        Edit::applyEdit(ourLines, ourPos, mode, e);
      }

      auto result = oracle_->simulate(buf, start.line, start.col, cmd);

      if (ourLines != result.lines || ourPos.line != result.row || ourPos.col != result.col) {
        if (failures < 10) {
          report << "Edit '" << cmd << "' at (" << start.line << "," << start.col << ")\n"
                 << "Initial:\n" << formatLines(buf)
                 << "Ours -> pos=(" << ourPos.line << "," << ourPos.col << ")\n"
                 << formatLines(ourLines)
                 << "Nvim -> pos=(" << result.row << "," << result.col << ")\n"
                 << formatLines(result.lines) << "\n";
        }
        failures++;
      }
    }
  }

  if (failures > 0) {
    FAIL() << failures << " paragraph delete oracle mismatch(es):\n" << report.str();
  }
}

TEST_F(InterpreterOracleConsistency, Oracle_LineOps) {
  verifyEdits({}, {"dd", "D", "d0"}, 20, 5);
}

TEST_F(InterpreterOracleConsistency, Oracle_CharJoin) {
  verifyEdits({}, {"x", "X", "J", "gJ"}, 20, 5);
}

// =============================================================================
// Oracle: Change tests
// =============================================================================

TEST_F(InterpreterOracleConsistency, Oracle_ChangeCommands) {
  // Change commands enter insert mode; we append Esc and compare buffer only
  unsigned baseSeed = testSeed();
  int failures = 0;
  ostringstream report;

  vector<string> cmds = {"ce", "cE", "cw", "cW", "cb", "cB", "cc", "C", "c0",
                          "ciw", "caw"};

  for (int i = 0; i < 20; i++) {
    RandomGen::seed(baseSeed + i);
    Lines buf = randomLines(RandomGen::range(2, 4), 5, 20);
    CursorPos start = randomPosition(buf);

    for (const auto& cmd : cmds) {
      // Our interpreter
      Lines ourLines = buf;
      CursorPos ourPos = start;
      Mode mode = Mode::Normal;
      auto edits = Edit::parseEdits(cmd);
      for (auto& e : edits) {
        Edit::applyEdit(ourLines, ourPos, mode, e);
      }

      // Neovim: append Esc to exit insert mode
      auto result = oracle_->simulate(buf, start.line, start.col, cmd + "\x1b");

      if (ourLines != result.lines) {
        if (failures < 10) {
          report << "Change '" << cmd << "' at (" << start.line << "," << start.col << ")\n"
                 << "Initial:\n" << formatLines(buf)
                 << "Ours:\n" << formatLines(ourLines)
                 << "Nvim:\n" << formatLines(result.lines) << "\n";
        }
        failures++;
      }
    }
  }

  if (failures > 0) {
    FAIL() << failures << " change oracle mismatch(es):\n" << report.str();
  }
}

// =============================================================================
// Oracle: Counted operations
// =============================================================================

TEST_F(InterpreterOracleConsistency, Oracle_CountedOps) {
  verifyEdits({}, {"2de", "2dw", "2dd", "dj", "dk", "2J", "2x"}, 10, 5);
}
