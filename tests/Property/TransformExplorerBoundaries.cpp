// Property: TransformExplorer boundary filtering must be sound and not
// over-strict. Every emitted structural delete/join must preserve protected
// prefix/suffix text, and every safe unbounded structural command should remain
// available when a boundary is applied.

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Boundary/TransformBoundary.h"
#include "Effort/EffortBank.h"
#include "Keyboard/Config.h"
#include "Optimizer/SequenceBinding.h"
#include "Optimizer/TransformOptimizer/TransformExplorer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Optimizer/TransformOptimizer/TransformState.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Property/PropertyTestUtils.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

struct BoundaryCase {
  Lines fullBuffer;
  Lines editRegion;
  Lines effectiveLines;
  TransformBoundary boundary;
  CursorPos begin;
  CursorPos end;
  CursorPos cursor;
  string protectedPrefix;
  string protectedSuffix;
};

// Full-buffer byte offset for a normal-mode cursor position. Newlines count
// because prefix/suffix preservation is checked on flattened buffers.
int flatOffset(const Lines& lines, CursorPos pos) {
  int offset = 0;
  for (int line = 0; line < pos.line; line++) {
    offset += static_cast<int>(lines[line].size()) + 1;
  }
  return offset + pos.col;
}

bool startsWith(string_view s, string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool endsWith(string_view s, string_view suffix) {
  return s.size() >= suffix.size() &&
         s.substr(s.size() - suffix.size()) == suffix;
}

string commandText(const SequenceBinding& cmd) {
  string base(cmd.base.seq.view());
  return cmd.count > 0 ? to_string(cmd.count) + base : base;
}

string formatCommands(const vector<string>& commands) {
  string out;
  for (const string& command : commands) {
    if (!out.empty()) out += ",";
    out += command;
  }
  return out;
}

bool dependsOnHiddenSentenceOrParagraphContext(string_view command) {
  return command.find('(') != string_view::npos ||
         command.find(')') != string_view::npos ||
         command.find('{') != string_view::npos ||
         command.find('}') != string_view::npos;
}

void appendRandomLines(Lines& lines, int count) {
  for (int i = 0; i < count; i++) {
    lines.push_back(randomLine(RandomGen::range(3, 8)));
  }
}

string guardText(string_view alphabet, int len) {
  return string(alphabet.substr(0, static_cast<size_t>(len)));
}

// Builds a full buffer, an embedded edit region, and the effective view that
// TransformExplorer actually sees: prefix + editRegion + suffix.
BoundaryCase generateBoundaryCase(int editLineCount) {
  BoundaryCase test;

  int linesAbove = RandomGen::range(0, 2);
  int linesBelow = RandomGen::range(0, 2);
  int prefixLen = RandomGen::range(0, 3);
  int suffixLen = RandomGen::range(0, 3);

  test.editRegion = randomLines(editLineCount, 3, 8);

  appendRandomLines(test.fullBuffer, linesAbove);

  string prefix = guardText("XYZ", prefixLen);
  string suffix = guardText("UVW", suffixLen);
  for (int line = 0; line < editLineCount; line++) {
    string fullLine = test.editRegion[line];
    if (line == 0) fullLine.insert(0, prefix);
    if (line == editLineCount - 1) fullLine += suffix;
    test.fullBuffer.push_back(fullLine);
  }

  appendRandomLines(test.fullBuffer, linesBelow);

  test.begin = CursorPos(linesAbove, prefixLen);
  test.end = CursorPos(
      linesAbove + editLineCount - 1,
      static_cast<int>(test.fullBuffer[linesAbove + editLineCount - 1].size()) -
          suffixLen);
  test.boundary = TransformBoundary(test.fullBuffer, test.begin, test.end);
  test.effectiveLines = test.boundary.withBoundary(test.editRegion);

  VimCore::WordBoundaryContext boundaryContext;
  boundaryContext.leftColOffset = test.boundary.leftColOffset();
  boundaryContext.rightColOffset = test.boundary.rightColOffset();
  boundaryContext.hasLinesAbove = test.boundary.hasLinesAbove();
  boundaryContext.hasLinesBelow = test.boundary.hasLinesBelow();

  int cursorLine = RandomGen::range(0, test.effectiveLines.lastLine());
  int contentStart = boundaryContext.contentStartCol(cursorLine);
  int contentEnd = boundaryContext.effectiveLineEnd(
      test.effectiveLines, cursorLine,
      cursorLine == test.effectiveLines.lastLine());
  if (contentEnd <= contentStart) {
    cursorLine = 0;
    contentStart = boundaryContext.contentStartCol(cursorLine);
    contentEnd = boundaryContext.effectiveLineEnd(
        test.effectiveLines, cursorLine,
        cursorLine == test.effectiveLines.lastLine());
  }
  test.cursor = CursorPos(cursorLine, RandomGen::range(contentStart, contentEnd - 1));

  string flat = test.fullBuffer.flatten();
  test.protectedPrefix = flat.substr(0, flatOffset(test.fullBuffer, test.begin));
  test.protectedSuffix = flat.substr(flatOffset(test.fullBuffer, test.end));

  return test;
}

// Translate an effective-lines cursor into full-buffer coordinates. Prefix is
// already present in effectiveLines, so the column is unchanged.
CursorPos fullCursor(const BoundaryCase& test) {
  return CursorPos(test.begin.line + test.cursor.line, test.cursor.col);
}

// The optimizer may delete inside the edit region, but never outside it.
bool preservesBoundary(const BoundaryCase& test, const Lines& result) {
  string flat = result.flatten();
  if (startsWith(flat, test.protectedPrefix) &&
      endsWith(flat, test.protectedSuffix)) {
    return true;
  }

  if (test.protectedSuffix.empty() &&
      !test.protectedPrefix.empty() &&
      test.protectedPrefix.back() == '\n') {
    return flat == test.protectedPrefix.substr(0, test.protectedPrefix.size() - 1);
  }

  if (test.protectedPrefix.empty() &&
      !test.protectedSuffix.empty() &&
      test.protectedSuffix.front() == '\n') {
    return flat == test.protectedSuffix.substr(1);
  }

  return false;
}

// Runs the same structural deletion sweep used by TransformOptimizer and
// returns the concrete command strings it would enqueue from this state.
vector<string> collectExplorerCommands(const BoundaryCase& test, bool bounded = true) {
  Config config = Config::uniform();
  TransformOptimizerParams params =
      TransformOptimizerParams{}.withMinCountRepeat(2).withMaxCountRepeat(4);
  EffortBank bank(config);
  TransformBoundary unbounded;
  const TransformBoundary& boundary = bounded ? test.boundary : unbounded;
  TransformExplorer explorer(
      boundary, params, config, bank,
      boundary.leftColOffset(), boundary.rightColOffset());
  TransformEditorState state(test.effectiveLines, test.cursor);

  set<string> commands;
  auto record = [&](const auto&, const SequenceBinding& cmd) {
    commands.insert(commandText(cmd));
  };
  auto recordLinewise = [&](LineRange, const SequenceBinding& cmd) {
    commands.insert(commandText(cmd));
  };
  auto recordJoin = [&](bool, const SequenceBinding& cmd) {
    commands.insert(commandText(cmd));
  };

  sweepExplorerStructurals(
      explorer, state, test.effectiveLines, test.cursor,
      boundary.leftColOffset(), boundary.rightColOffset(),
      params.minPrefixCount,
      record, recordLinewise, recordJoin, recordLinewise, recordJoin);

  return vector<string>(commands.begin(), commands.end());
}

class TransformExplorerBoundaryPropertyTest {
 public:
  void EmittedDeletesPreserveBoundaries(uint32_t seed) {
    runSeedDriverCases(seed, 10, [&] {
      BoundaryCase test = generateBoundaryCase(RandomGen::range(1, 4));
      vector<string> commands = collectExplorerCommands(test);
      ASSERT_FALSE(commands.empty());

      for (const string& command : commands) {
        SCOPED_TRACE(::testing::Message()
                     << "command='" << command << "'"
                     << " cursor=" << test.cursor
                     << " fullCursor=" << fullCursor(test)
                     << "\nfullBuffer=" << test.fullBuffer
                     << "\neffectiveLines=" << test.effectiveLines);
        SimulationResult result = oracle_.simulate(
            test.fullBuffer, fullCursor(test).line, fullCursor(test).col, command);
        EXPECT_EQ(result.mode, Mode::Normal);
        EXPECT_TRUE(preservesBoundary(test, result.lines));
      }
    });
  }

  // Completeness check for local commands. Sentence/paragraph operators can be
  // safe in the full buffer while still ambiguous from slice-local context.
  void SafeLocalUnboundedDeletesRemainAvailable(uint32_t seed) {
    runSeedDriverCases(seed, 8, [&] {
      BoundaryCase test = generateBoundaryCase(RandomGen::range(1, 4));
      vector<string> emitted = collectExplorerCommands(test);
      set<string> emittedSet(emitted.begin(), emitted.end());
      vector<string> unbounded = collectExplorerCommands(test, /*bounded=*/false);

      for (const string& command : unbounded) {
        if (emittedSet.contains(command)) continue;
        if (dependsOnHiddenSentenceOrParagraphContext(command)) continue;
        SimulationResult result = oracle_.simulate(
            test.fullBuffer, fullCursor(test).line, fullCursor(test).col, command);
        bool changed = result.lines != test.fullBuffer;
        bool safe = changed &&
                    result.mode == Mode::Normal &&
                    preservesBoundary(test, result.lines);
        if (!safe) continue;

        SCOPED_TRACE(::testing::Message()
                     << "boundary-pruned safe command='" << command << "'"
                     << " cursor=" << test.cursor
                     << " fullCursor=" << fullCursor(test)
                     << "\nfullBuffer=" << test.fullBuffer
                     << "\neffectiveLines=" << test.effectiveLines
                     << "\nemitted=" << formatCommands(emitted)
                     << "\nunbounded=" << formatCommands(unbounded));
        EXPECT_TRUE(emittedSet.contains(command));
      }
    });
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(TransformExplorerBoundaryPropertyTest, EmittedDeletesPreserveBoundaries)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000));

FUZZ_TEST_F(TransformExplorerBoundaryPropertyTest, SafeLocalUnboundedDeletesRemainAvailable)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000));
