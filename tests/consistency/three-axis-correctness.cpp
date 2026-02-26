// tests/consistency/three-axis-correctness.cpp
//
// Three-axis command-level correctness checks:
// 1) model implementation used by optimizer exploration
// 2) interpreter implementation used for replay/execution
// 3) Neovim oracle ground truth

#include <gtest/gtest.h>

#include <cctype>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Boundary/EditBoundary.h"
#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MotionInterpreter.h"
#include "Keyboard/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizerParams.h"
#include "Optimizer/EditOptimizer/EditSearchContext.h"
#include "Optimizer/EditOptimizer/EditState.h"
#include "Optimizer/SequenceBinding.h"
#include "Types/CursorPos.h"
#include "Types/LineRange.h"
#include "Types/Mode.h"
#include "Types/NavContext.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimMotionUtils.h"

using namespace std;

namespace {

struct BufferPosResult {
  Lines lines;
  CursorPos pos;
};

string formatLines(const Lines& lines) {
  ostringstream oss;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    oss << "  [" << i << "] \"" << lines[i] << "\"\n";
  }
  return oss.str();
}

string commandString(const SequenceBinding& sb) {
  string cmd;
  if (sb.count > 0) cmd += to_string(sb.count);
  cmd += string(sb.base.seq.view());
  return cmd;
}

BufferPosResult runInterpreter(const Lines& lines, CursorPos startPos, const string& cmd,
                               bool hasLinesBelow = false,
                               int leftColOffset = 0,
                               int rightColOffset = 0,
                               bool hasLinesAbove = false) {
  Lines simLines = lines;
  CursorPos simPos = startPos;
  Mode simMode = Mode::Normal;
  string lastEditCmd;
  for (const ParsedEdit& op : Edit::parseEdits(cmd)) {
    Edit::applyEdit(simLines, simPos, simMode, op, &lastEditCmd,
                    hasLinesBelow, leftColOffset, rightColOffset, hasLinesAbove);
  }
  return {simLines, simPos};
}

CursorPos runMotionModel(CursorPos pos, const Lines& lines, const string& cmd) {
  if (cmd == "w") {
    VimCore::motionW(pos, lines, false);
    pos.setCol(VimCore::clampCol(lines, pos.col, pos.line));
    return pos;
  }
  if (cmd == "W") {
    VimCore::motionW(pos, lines, true);
    pos.setCol(VimCore::clampCol(lines, pos.col, pos.line));
    return pos;
  }
  if (cmd == "b") {
    VimCore::motionB(pos, lines, false);
    return pos;
  }
  if (cmd == "B") {
    VimCore::motionB(pos, lines, true);
    return pos;
  }
  if (cmd == "e") {
    VimCore::motionE(pos, lines, false);
    pos.setCol(VimCore::clampCol(lines, pos.col, pos.line));
    return pos;
  }
  if (cmd == "E") {
    VimCore::motionE(pos, lines, true);
    pos.setCol(VimCore::clampCol(lines, pos.col, pos.line));
    return pos;
  }
  if (cmd == "ge") {
    VimCore::motionGe(pos, lines, false);
    return pos;
  }
  if (cmd == "gE") {
    VimCore::motionGe(pos, lines, true);
    return pos;
  }
  if (cmd == "0") {
    pos.setCol(0);
    return pos;
  }
  if (cmd == "^") {
    int len = static_cast<int>(lines[pos.line].size());
    int c = 0;
    while (c < len && isspace(static_cast<unsigned char>(lines[pos.line][c]))) ++c;
    if (c >= len && len > 0) c = len - 1;
    pos.setCol(c);
    return pos;
  }
  if (cmd == "$") {
    int len = static_cast<int>(lines[pos.line].size());
    pos.col = (len == 0) ? 0 : len - 1;
    pos.targetCol = TARGETCOL_EOL;
    return pos;
  }
  if (cmd == "h") {
    VimCore::moveCol(pos, lines, -1);
    return pos;
  }
  if (cmd == "l") {
    VimCore::moveCol(pos, lines, 1);
    return pos;
  }
  if (cmd == "j") {
    VimCore::moveLine(pos, lines, 1);
    return pos;
  }
  if (cmd == "k") {
    VimCore::moveLine(pos, lines, -1);
    return pos;
  }
  if (cmd == ")") {
    VimCore::motionSentenceNext(pos, lines);
    return pos;
  }
  if (cmd == "(") {
    VimCore::motionSentencePrev(pos, lines);
    return pos;
  }
  if (cmd == "}") {
    VimCore::motionParagraphNext(pos, lines);
    return pos;
  }
  if (cmd == "{") {
    VimCore::motionParagraphPrev(pos, lines);
    return pos;
  }
  ADD_FAILURE() << "Unknown motion command in model: " << cmd;
  return pos;
}

optional<BufferPosResult> maybeInsertUnique(unordered_map<string, BufferPosResult>& byCmd,
                                            const string& cmd,
                                            const BufferPosResult& value) {
  auto it = byCmd.find(cmd);
  if (it == byCmd.end()) {
    byCmd.insert({cmd, value});
    return nullopt;
  }
  if (it->second.lines != value.lines || it->second.pos != value.pos) {
    return value;
  }
  return nullopt;
}

}  // namespace

class ThreeAxisCorrectness : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  EditOptimizerParams params = EditOptimizerParams().withMaxResults(1);

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};

unique_ptr<NeovimOracle> ThreeAxisCorrectness::oracle;

TEST_F(ThreeAxisCorrectness, SingleMotions_ModelInterpreterOracleAgree) {
  const vector<string> motions = {
      "w", "W", "b", "B", "e", "E", "ge", "gE",
      "0", "^", "$", "h", "l", "j", "k", ")", "(", "}", "{",
  };

  const vector<Lines> corpora = {
      {"hello world", "alpha,beta gamma"},
      {"  lead", "", "tail  word"},
      {"One sentence. Next sentence! Last?", "  Indented sentence."},
      {"para one", "line two", "", "para two", "line three"},
      {"a..b,c", "  d e", "f"},
  };

  int mismatches = 0;
  ostringstream report;

  for (const Lines& lines : corpora) {
    for (int row = 0; row < static_cast<int>(lines.size()); ++row) {
      int cols = lines[row].effectiveSize();
      for (int col = 0; col < cols; ++col) {
        CursorPos start(row, col);
        for (const string& cmd : motions) {
          CursorPos modelPos = runMotionModel(start, lines, cmd);
          CursorPos interpreterPos = simulateMotions(start, cmd, lines, NavContext());
          auto oracleResult = oracle->simulate(lines, start.line, start.col, cmd);
          CursorPos oraclePos(oracleResult.row, oracleResult.col);

          if (modelPos != interpreterPos || modelPos != oraclePos) {
            if (mismatches < 12) {
              report << "Motion '" << cmd << "' at (" << start.line << "," << start.col << ")\n"
                     << "Buffer:\n" << formatLines(lines)
                     << "Model      -> (" << modelPos.line << "," << modelPos.col << ")\n"
                     << "Interpreter-> (" << interpreterPos.line << "," << interpreterPos.col << ")\n"
                     << "Neovim     -> (" << oraclePos.line << "," << oraclePos.col << ")\n\n";
            }
            ++mismatches;
          }
        }
      }
    }
  }

  if (mismatches > 0) {
    FAIL() << mismatches << " motion triad mismatch(es)\n" << report.str();
  }
}

TEST_F(ThreeAxisCorrectness, SingleEdits_ModelInterpreterOracleAgree) {
  const vector<Lines> corpora = {
      {"aaa", "b"},
      {"cabd, cb,a ", "c..cf.cd.."},
      {"first sentence. second sentence?", "", "third sentence!"},
      {"  alpha beta", "gamma", "", "delta  epsilon", "zeta"},
      {"x y z", "line2 with words", "line3"},
  };

  set<string> coveredCommands;
  int mismatches = 0;
  ostringstream report;

  for (const Lines& initial : corpora) {
    EditBoundary boundary(initial, CursorPos(0, 0), initial.endPos());
    EditSearchContext ctx(initial, boundary, params, config);
    const Lines& effLines = ctx.effectiveLines;

    for (int row = 0; row < static_cast<int>(effLines.size()); ++row) {
      int cols = effLines[row].effectiveSize();
      for (int col = 0; col < cols; ++col) {
        CursorPos start(row, col);
        EditState state(effLines, start, 0, 0.0);

        unordered_map<string, BufferPosResult> modelByCmd;
        optional<BufferPosResult> duplicateConflict;
        string conflictCmd;

        auto onDeletion = [&](const Range& range, const SequenceBinding& sb) {
          EditState after = state.afterDeletion(range);
          string cmd = commandString(sb);
          auto conflict = maybeInsertUnique(modelByCmd, cmd,
                                            {after.getLines(), after.getPos()});
          if (conflict.has_value() && !duplicateConflict.has_value()) {
            duplicateConflict = conflict;
            conflictCmd = cmd;
          }
        };

        auto onLinewise = [&](int line, const SequenceBinding& sb) {
          EditState after = state.afterLinewiseDeletion(line, /*hasLinesBelow=*/false);
          CursorPos afterPos = after.getPos();
          if (afterPos.line < 0 ||
              afterPos.line >= static_cast<int>(after.getLines().size())) {
            return;
          }
          string cmd = commandString(sb);
          auto conflict = maybeInsertUnique(modelByCmd, cmd,
                                            {after.getLines(), after.getPos()});
          if (conflict.has_value() && !duplicateConflict.has_value()) {
            duplicateConflict = conflict;
            conflictCmd = cmd;
          }
        };

        auto onJoin = [&](bool addSpace, const SequenceBinding& sb) {
          EditState after = state.afterJoin(addSpace);
          string cmd = commandString(sb);
          auto conflict = maybeInsertUnique(modelByCmd, cmd,
                                            {after.getLines(), after.getPos()});
          if (conflict.has_value() && !duplicateConflict.has_value()) {
            duplicateConflict = conflict;
            conflictCmd = cmd;
          }
        };

        ctx.exploreAllDeletions(state, onDeletion, onLinewise, onJoin);

        if (duplicateConflict.has_value()) {
          FAIL() << "Inconsistent model result for command '" << conflictCmd
                 << "' at (" << start.line << "," << start.col << ")\n"
                 << "Buffer:\n" << formatLines(effLines);
        }

        for (const auto& [cmd, modelResult] : modelByCmd) {
          coveredCommands.insert(cmd);

          BufferPosResult interpreterResult = runInterpreter(
              effLines, start, cmd,
              /*hasLinesBelow=*/false,
              /*leftColOffset=*/0,
              /*rightColOffset=*/0,
              /*hasLinesAbove=*/false);

          auto oracleResult = oracle->simulate(effLines, start.line, start.col, cmd);
          BufferPosResult nvimResult{oracleResult.lines, CursorPos(oracleResult.row, oracleResult.col)};

          bool modelVsInterpreter = (modelResult.lines == interpreterResult.lines &&
                                     modelResult.pos == interpreterResult.pos);
          bool modelVsOracle = (modelResult.lines == nvimResult.lines &&
                                modelResult.pos == nvimResult.pos);

          if (!modelVsInterpreter || !modelVsOracle) {
            if (mismatches < 12) {
              report << "Edit '" << cmd << "' at (" << start.line << "," << start.col << ")\n"
                     << "Buffer:\n" << formatLines(effLines)
                     << "Model -> pos=(" << modelResult.pos.line << "," << modelResult.pos.col << ")\n"
                     << formatLines(modelResult.lines)
                     << "Interpreter -> pos=(" << interpreterResult.pos.line << "," << interpreterResult.pos.col << ")\n"
                     << formatLines(interpreterResult.lines)
                     << "Neovim -> pos=(" << nvimResult.pos.line << "," << nvimResult.pos.col << ")\n"
                     << formatLines(nvimResult.lines) << "\n";
            }
            ++mismatches;
          }
        }
      }
    }
  }

  const set<string> required = {
      "de", "dE", "dw", "dW", "db", "dB", "dge", "dgE",
      "diw", "daw", "diW", "daW",
      "D", "d0", "dd", "x", "X",
      // d{ is intentionally omitted: backward multi-line paragraph delete
      // is not emitted by EditExplorer yet (see exploreParagraphEdits<false>).
      "d}", "d)", "d(", "J", "gJ",
  };

  vector<string> missing;
  for (const string& cmd : required) {
    if (coveredCommands.find(cmd) == coveredCommands.end()) missing.push_back(cmd);
  }

  if (!missing.empty()) {
    ostringstream missingReport;
    for (const string& cmd : missing) {
      missingReport << cmd << " ";
    }
    FAIL() << "Coverage gap in single-command triad test. Missing commands: "
           << missingReport.str();
  }

  if (mismatches > 0) {
    FAIL() << mismatches << " edit triad mismatch(es)\n" << report.str();
  }
}
