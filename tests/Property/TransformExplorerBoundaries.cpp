// Property: TransformExplorer boundary filtering must be sound and not
// over-strict. Every emitted structural delete/join must preserve protected
// prefix/suffix text, and every safe unbounded structural command should remain
// available when a boundary is applied.

#include <algorithm>
#include <set>
#include <sstream>
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
#include "Property/PropertyDomains.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"

using namespace std;

namespace {

int clampedIndex(int value, int size) {
  return std::clamp(value, 0, size - 1);
}

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

struct BoundaryCaseSpec {
  vector<string> editRegion;
  vector<string> linesAbove;
  vector<string> linesBelow;
  string prefix;
  string suffix;
  int cursorIndex;
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

void appendRawLines(Lines& lines, const vector<string>& rawLines) {
  for (const string& line : rawLines) {
    lines.push_back(line);
  }
}

bool hasEditableText(const vector<string>& lines) {
  return any_of(lines.begin(), lines.end(),
                [](const string& line) { return !line.empty(); });
}

VimCore::WordBoundaryContext wordBoundaryContext(const BoundaryCase& test) {
  VimCore::WordBoundaryContext boundaryContext;
  boundaryContext.leftColOffset = test.boundary.leftColOffset();
  boundaryContext.rightColOffset = test.boundary.rightColOffset();
  boundaryContext.hasLinesAbove = test.boundary.hasLinesAbove();
  boundaryContext.hasLinesBelow = test.boundary.hasLinesBelow();
  return boundaryContext;
}

vector<CursorPos> editableCursorPositions(const BoundaryCase& test) {
  VimCore::WordBoundaryContext boundaryContext = wordBoundaryContext(test);
  vector<CursorPos> positions;
  for (int line = 0; line <= test.effectiveLines.lastLine(); line++) {
    int contentStart = boundaryContext.contentStartCol(line);
    int contentEnd = boundaryContext.effectiveLineEnd(
        test.effectiveLines, line, line == test.effectiveLines.lastLine());
    for (int col = contentStart; col < contentEnd; col++) {
      positions.emplace_back(line, col);
    }
  }
  if (positions.empty()) {
    positions.emplace_back(0, 0);
  }
  return positions;
}

BoundaryCase buildBoundaryCase(const BoundaryCaseSpec& spec) {
  BoundaryCase test;
  test.editRegion = Lines(spec.editRegion);

  appendRawLines(test.fullBuffer, spec.linesAbove);

  for (int line = 0; line <= test.editRegion.lastLine(); line++) {
    string fullLine = test.editRegion[line];
    if (line == 0) fullLine.insert(0, spec.prefix);
    if (line == test.editRegion.lastLine()) fullLine += spec.suffix;
    test.fullBuffer.push_back(fullLine);
  }

  appendRawLines(test.fullBuffer, spec.linesBelow);

  int linesAbove = static_cast<int>(spec.linesAbove.size());
  int suffixLen = static_cast<int>(spec.suffix.size());
  int lastEditFullLine = linesAbove + test.editRegion.lastLine();
  test.begin = CursorPos(linesAbove, static_cast<int>(spec.prefix.size()));
  test.end = CursorPos(
      lastEditFullLine,
      static_cast<int>(test.fullBuffer[lastEditFullLine].size()) - suffixLen);
  test.boundary = TransformBoundary(test.fullBuffer, test.begin, test.end);
  test.effectiveLines = test.boundary.withBoundary(test.editRegion);

  vector<CursorPos> positions = editableCursorPositions(test);
  test.cursor = positions[clampedIndex(
      spec.cursorIndex, static_cast<int>(positions.size()))];

  string flat = test.fullBuffer.flatten();
  test.protectedPrefix = flat.substr(0, flatOffset(test.fullBuffer, test.begin));
  test.protectedSuffix = flat.substr(flatOffset(test.fullBuffer, test.end));

  return test;
}

string formatSpec(const BoundaryCaseSpec& spec) {
  ostringstream out;
  out << "rawCursorIndex=" << spec.cursorIndex
      << " prefix='" << spec.prefix << "'"
      << " suffix='" << spec.suffix << "'";
  return out.str();
}

auto BoundaryCaseSpecDomain() {
  return fuzztest::StructOf<BoundaryCaseSpec>(
      fuzztest::Filter(
          hasEditableText,
          fuzztest::VectorOf(fuzztest::InRegexp("[abcdef .,]{0,8}"))
              .WithMinSize(1)
              .WithMaxSize(4)),
      fuzztest::VectorOf(fuzztest::InRegexp("[abcdef .,]{0,8}")).WithMaxSize(2),
      fuzztest::VectorOf(fuzztest::InRegexp("[abcdef .,]{0,8}")).WithMaxSize(2),
      fuzztest::InRegexp("[XYZ]{0,3}"),
      fuzztest::InRegexp("[UVW]{0,3}"),
      fuzztest::InRange<int>(0, 96));
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
  auto recordCountedLinewise = [&](LineRange, const SequenceBinding& cmd) {
    commands.insert(commandText(cmd));
  };
  auto recordJoin = [&](bool, const SequenceBinding& cmd) {
    commands.insert(commandText(cmd));
  };

  sweepExplorerStructurals(
      explorer, state, test.effectiveLines, test.cursor,
      boundary.leftColOffset(), boundary.rightColOffset(),
      params.minPrefixCount,
      record, recordJoin, recordCountedLinewise, recordJoin);

  return vector<string>(commands.begin(), commands.end());
}

class TransformExplorerBoundaryPropertyTest {
 public:
  void EmittedDeletesPreserveBoundaries(const BoundaryCaseSpec& spec) {
    BoundaryCase test = buildBoundaryCase(spec);
    vector<string> commands = collectExplorerCommands(test);
    if (commands.empty()) return;

    for (const string& command : commands) {
      SCOPED_TRACE(::testing::Message()
                   << "command='" << command << "'"
                   << " cursor=" << test.cursor
                   << " fullCursor=" << fullCursor(test)
                   << " " << formatSpec(spec)
                   << "\nfullBuffer=" << test.fullBuffer
                   << "\neditRegion=" << test.editRegion
                   << "\neffectiveLines=" << test.effectiveLines);
      SimulationResult result = oracle_.simulate(
          test.fullBuffer, fullCursor(test).line, fullCursor(test).col, command);
      EXPECT_EQ(result.mode, Mode::Normal);
      EXPECT_TRUE(preservesBoundary(test, result.lines));
    }
  }

  // Completeness check for local commands. Sentence/paragraph operators can be
  // safe in the full buffer while still ambiguous from slice-local context.
  void SafeLocalUnboundedDeletesRemainAvailable(const BoundaryCaseSpec& spec) {
    BoundaryCase test = buildBoundaryCase(spec);
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
                   << " " << formatSpec(spec)
                   << "\nfullBuffer=" << test.fullBuffer
                   << "\neditRegion=" << test.editRegion
                   << "\neffectiveLines=" << test.effectiveLines
                   << "\nemitted=" << formatCommands(emitted)
                   << "\nunbounded=" << formatCommands(unbounded));
      EXPECT_TRUE(emittedSet.contains(command));
    }
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(TransformExplorerBoundaryPropertyTest, EmittedDeletesPreserveBoundaries)
    .WithDomains(BoundaryCaseSpecDomain());

FUZZ_TEST_F(TransformExplorerBoundaryPropertyTest, SafeLocalUnboundedDeletesRemainAvailable)
    .WithDomains(BoundaryCaseSpecDomain());
