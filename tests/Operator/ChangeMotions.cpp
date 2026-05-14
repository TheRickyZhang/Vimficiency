// tests/Operator/ChangeMotions.cpp
//
// Oracle-backed coverage for supported Normal-mode change operators.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="ChangeMotionsTest.*"

#include <cassert>

#include <gtest/gtest.h>

#include "Interpreter/EditInterpreter.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Mode.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimEditUtils.h"

using namespace std;

class ChangeMotionsTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};

unique_ptr<NeovimOracle> ChangeMotionsTest::oracle;

struct ChangeMotionCase {
  const char* name;
  Lines source;
  CursorPos pos;
  const char* cmd;
};

static void applyParsedSequence(Lines& lines, CursorPos& pos, Mode& mode, string_view sequence) {
  string lastEditCmd;
  auto parsed = Edit::parseEdits(sequence);
  assert(parsed);
  for (const ParsedEdit& edit : *parsed) {
    Edit::applyEdit(lines, pos, mode, edit, &lastEditCmd);
  }
}

static void applyChangeThenType(Lines& lines, CursorPos& pos, Mode& mode, string_view cmd) {
  applyParsedSequence(lines, pos, mode, cmd);
  ASSERT_EQ(mode, Mode::Insert) << "change command should enter Insert mode: " << cmd;
  VimCore::insertText(lines, pos, "X");
  applyParsedSequence(lines, pos, mode, "<Esc>");
}

TEST_F(ChangeMotionsTest, SupportedChangeMotionsMatchNeovim) {
  const vector<ChangeMotionCase> cases = {
      {"cw", {"alpha beta"}, {0, 1}, "cw"},
      {"cw-whitespace", {"alpha  beta"}, {0, 5}, "cw"},
      {"cW", {"alpha-beta gamma"}, {0, 2}, "cW"},
      {"cW-whitespace", {"alpha-beta  gamma"}, {0, 10}, "cW"},
      {"ce", {"alpha beta"}, {0, 1}, "ce"},
      {"cE", {"alpha-beta gamma"}, {0, 1}, "cE"},
      {"cb", {"alpha beta"}, {0, 6}, "cb"},
      {"cB", {"alpha-beta gamma"}, {0, 11}, "cB"},
      {"cge", {"alpha beta gamma"}, {0, 11}, "cge"},
      {"cgE", {"alpha-beta gamma"}, {0, 11}, "cgE"},

      {"c0", {"alpha beta"}, {0, 5}, "c0"},
      {"c^", {"  alpha beta"}, {0, 7}, "c^"},
      {"c$", {"alpha beta"}, {0, 2}, "c$"},
      {"C", {"alpha beta"}, {0, 2}, "C"},
      {"cc", {"  alpha"}, {0, 3}, "cc"},
      {"S", {"  alpha"}, {0, 3}, "S"},
      {"cj", {"  alpha", "  beta", "gamma"}, {0, 2}, "cj"},
      {"ck", {"  alpha", "  beta", "gamma"}, {1, 2}, "ck"},

      {"c}-backed-up", {"abc", "", "def"}, {0, 2}, "c}"},
      {"c}-col-zero-boundary", {"abc", "", "def"}, {0, 0}, "c}"},
      {"c}-eof", {"abc", "def"}, {0, 0}, "c}"},
      {"c{-col-zero-boundary", {"abc", "", "def"}, {2, 0}, "c{"},
      {"c)-col-zero-boundary", {"End.", "Start"}, {0, 0}, "c)"},
      {"c)-backed-up", {"be.df.", ".ee  cb"}, {0, 2}, "c)"},
      {"c(-col-zero-boundary", {"End.", "Start"}, {1, 0}, "c("},
      {"c(-backed-up", {"End. xyz", "abc"}, {1, 0}, "c("},
  };

  for (const ChangeMotionCase& test : cases) {
    string sequence = string(test.cmd) + "X<Esc>";
    auto nvim = oracle->simulate(test.source, test.pos.line, test.pos.col, sequence);

    Lines oursLines = test.source;
    CursorPos oursPos = test.pos;
    Mode oursMode = Mode::Normal;
    applyChangeThenType(oursLines, oursPos, oursMode, test.cmd);

    EXPECT_EQ(oursLines, nvim.lines)
        << test.name << " lines mismatch for '" << sequence << "' from " << test.pos
        << "\nsource=" << test.source
        << "\nours=" << oursLines
        << "\nnvim=" << nvim.lines;
    EXPECT_EQ(oursPos, CursorPos(nvim.row, nvim.col))
        << test.name << " cursor mismatch for '" << sequence << "'";
    EXPECT_EQ(oursMode, nvim.mode)
        << test.name << " mode mismatch for '" << sequence << "'";
  }
}
