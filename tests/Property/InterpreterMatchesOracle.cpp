// Property: generated Vim input sequences must leave our interpreter in the
// same buffer/cursor/mode state as Neovim. Focused generators cover movement,
// char-find repeat state, edits, and small mixed normal-mode sequences.

#include <array>
#include <algorithm>
#include <cassert>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/MovementInterpreter.h"
#include "Property/PropertyDomains.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/InterpreterModelReplay.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"

using namespace std;

namespace {

int clampedIndex(int value, int size) {
  return std::clamp(value, 0, size - 1);
}

struct MovementTokenSpec {
  int kind;
  int count;
  char target;
};

struct MovementSequenceSpec {
  vector<string> lines;
  int cursorIndex;
  vector<MovementTokenSpec> tokens;
};

struct CharFindRepeatSpec {
  string line;
  int cursorIndex;
  int commandKind;
  char target;
  vector<bool> repeatDirections;
};

struct EditCommandSpec {
  int kind;
  int cursorIndex;
  char replacement;
};

struct ParagraphSentenceChangeSpec {
  int kind;
  string firstLine;
  string secondLine;
  string payload;
  int cursorIndex;
};

struct MixedTokenSpec {
  int choice;
  char replacement;
};

struct MixedNormalSequenceSpec {
  string line;
  int cursorIndex;
  vector<MixedTokenSpec> tokens;
};

auto FindTargetDomain() {
  return fuzztest::ElementOf(vector<char>{
      'a', 'b', 'c', 'd', 'e', 'f', ' ', '.', ','});
}

auto InsertPayloadDomain(int maxLen) {
  return fuzztest::StringOf(fuzztest::LowerChar())
      .WithMinSize(1)
      .WithMaxSize(maxLen);
}

string optionalCount(int rawCount, int maxCount) {
  int count = std::clamp(rawCount, 0, maxCount);
  return count < 2 ? "" : to_string(count);
}

char findCommand(int rawKind) {
  static constexpr array<char, 4> commands = {'f', 'F', 't', 'T'};
  return commands[clampedIndex(rawKind, static_cast<int>(commands.size()))];
}

string repeatCommand(bool forward, int rawCount) {
  return optionalCount(rawCount, /*maxCount=*/4) + (forward ? ";" : ",");
}

string basicMotionToken(const MovementTokenSpec& token) {
  static constexpr array<string_view, 15> countable = {
      "h", "l", "j", "k", "$", "gg", "G",
      "w", "W", "b", "B", "e", "E", "ge", "gE",
  };
  static constexpr array<string_view, 2> uncounted = {"0", "^"};

  int kind = std::clamp(token.kind, 0, 18);
  if (kind < static_cast<int>(countable.size())) {
    return optionalCount(token.count, /*maxCount=*/6) + string(countable[kind]);
  }
  if (kind < static_cast<int>(countable.size() + uncounted.size())) {
    return string(uncounted[kind - static_cast<int>(countable.size())]);
  }

  string seq = optionalCount(token.count, /*maxCount=*/4);
  seq += findCommand(kind - static_cast<int>(countable.size() + uncounted.size()));
  seq += token.target;
  return seq;
}

string movementSequence(const vector<MovementTokenSpec>& tokens) {
  string seq;
  bool hasFindState = false;
  for (const auto& token : tokens) {
    int kind = std::clamp(token.kind, 0, 19);
    if (kind == 19) {
      seq += hasFindState ? repeatCommand(token.target != ',', token.count) : "0";
      continue;
    }

    if (kind >= 17) {
      hasFindState = true;
    }
    seq += basicMotionToken(token);
  }
  return seq;
}

string repeatChain(const vector<bool>& directions) {
  string chain;
  for (bool forward : directions) {
    chain += forward ? ';' : ',';
  }
  return chain;
}

auto MovementTokenSpecDomain() {
  return fuzztest::StructOf<MovementTokenSpec>(
      fuzztest::InRange<int>(0, 19),
      fuzztest::InRange<int>(0, 6),
      FindTargetDomain());
}

auto MovementSequenceSpecDomain() {
  return fuzztest::StructOf<MovementSequenceSpec>(
      PropertyDomains::LineVecDomain(1, 8, 0, 100),
      fuzztest::InRange<int>(0, 800),
      fuzztest::VectorOf(MovementTokenSpecDomain())
          .WithMinSize(1)
          .WithMaxSize(12));
}

auto CharFindRepeatSpecDomain() {
  return fuzztest::StructOf<CharFindRepeatSpec>(
      PropertyDomains::LineTextDomain(2, 24),
      fuzztest::InRange<int>(0, 23),
      fuzztest::InRange<int>(0, 3),
      FindTargetDomain(),
      fuzztest::VectorOf(fuzztest::Arbitrary<bool>()).WithMaxSize(4));
}

auto EditCommandSpecDomain() {
  return fuzztest::StructOf<EditCommandSpec>(
      fuzztest::InRange<int>(0, 8),
      fuzztest::InRange<int>(0, 5),
      fuzztest::LowerChar());
}

auto ParagraphSentenceChangeSpecDomain() {
  return fuzztest::StructOf<ParagraphSentenceChangeSpec>(
      fuzztest::InRange<int>(0, 4),
      PropertyDomains::LineTextDomain(0, 6),
      PropertyDomains::LineTextDomain(0, 6),
      InsertPayloadDomain(5),
      fuzztest::InRange<int>(0, 8));
}

auto MixedTokenSpecDomain() {
  return fuzztest::StructOf<MixedTokenSpec>(
      fuzztest::InRange<int>(0, 7),
      fuzztest::LowerChar());
}

auto MixedNormalSequenceSpecDomain() {
  return fuzztest::StructOf<MixedNormalSequenceSpec>(
      PropertyDomains::LineTextDomain(1, 24),
      fuzztest::InRange<int>(0, 23),
      fuzztest::VectorOf(MixedTokenSpecDomain())
          .WithMinSize(3)
          .WithMaxSize(10));
}

class InterpreterMatchesOracle {
 public:
  void MovementSequences(const MovementSequenceSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    string seq = movementSequence(spec.tokens);

    expectMovementModelMatchesOracle(lines, cursor, seq);
  }

  void CharFindRepeatState(const CharFindRepeatSpec& spec) {
    Lines lines{spec.line};
    CursorPos cursor(
        0, clampedIndex(spec.cursorIndex, lines[0].effectiveSize()));

    string seq;
    seq += findCommand(spec.commandKind);
    seq += spec.target;
    seq += repeatChain(spec.repeatDirections);

    expectMovementModelMatchesOracle(lines, cursor, seq);
  }

  void EditCommands(const EditCommandSpec& spec) {
    auto test = editCommandCase(spec);
    expectSequenceModelMatchesOracle(test.initial, test.cursor, test.seq);
  }

  void ParagraphSentenceChangeOperators(
      const ParagraphSentenceChangeSpec& spec) {
    auto test = paragraphSentenceChangeCase(spec);
    expectSequenceModelMatchesOracle(test.initial, test.cursor, test.seq);
  }

  void MixedNormalModeSequences(const MixedNormalSequenceSpec& spec) {
    Lines lines{spec.line};
    CursorPos cursor =
        lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    Lines initial = lines;
    CursorPos initialCursor = cursor;
    string seq;

    for (const auto& tokenSpec : spec.tokens) {
      string token = safeNormalToken(lines, cursor, tokenSpec);
      seq += token;
      applySequenceToken(lines, cursor, token);
    }

    auto [expectedLines, expectedCursor, expectedMode] =
        applyUserSequence(initial, initialCursor, seq);
    EXPECT_EQ(expectedLines, lines);
    EXPECT_EQ(expectedCursor, cursor);
    EXPECT_EQ(expectedMode, Mode::Normal);

    OracleReplay::expectMatchesOracle(
        oracle_, initial, initialCursor, seq,
        expectedLines, expectedCursor, expectedMode);
  }

 private:
  struct EditCommandCase {
    Lines initial;
    CursorPos cursor;
    string seq;
  };

  NeovimOracle oracle_{};

  EditCommandCase editCommandCase(const EditCommandSpec& spec) {
    switch (clampedIndex(spec.kind, 9)) {
      case 0:
        return {Lines{"abcdef"}, CursorPos(0, cursorCol(spec, 6)), "x"};
      case 1:
        return {Lines{"abcdef"}, CursorPos(0, 1 + cursorCol(spec, 5)), "X"};
      case 2:
        return {Lines{"abcdef"}, CursorPos(0, cursorCol(spec, 6)),
                string("r") + spec.replacement};
      case 3:
        return {Lines{"abcdef"}, CursorPos(0, cursorCol(spec, 6)), "~"};
      case 4:
        return {Lines{"aaa bbb ccc"}, CursorPos(0, 0), "dw"};
      case 5:
        return {Lines{"aaa bbb ccc"}, CursorPos(0, 0), "de"};
      case 6:
        return {Lines{"aaa bbb ccc"}, CursorPos(0, 4), "db"};
      case 7:
        return {Lines{"aaa", "bbb", "ccc"}, CursorPos(1, 0), "dd"};
      default:
        return {Lines{"aaa", "bbb", "ccc"}, CursorPos(0, 0), "J"};
    }
  }

  int cursorCol(const EditCommandSpec& spec, int lineSize) {
    return clampedIndex(spec.cursorIndex, lineSize);
  }

  EditCommandCase paragraphSentenceChangeCase(
      const ParagraphSentenceChangeSpec& spec) {
    string payload = spec.payload;
    switch (clampedIndex(spec.kind, 5)) {
      case 0:
        return {
            Lines{spec.firstLine, spec.secondLine},
            CursorPos(spec.cursorIndex % 2, 0),
            "c}" + payload + "<Esc>"};
      case 1:
        return {
            Lines{spec.firstLine, "", spec.secondLine},
            CursorPos(0, lineCol(spec.firstLine, spec.cursorIndex)),
            "c}" + payload + "<Esc>"};
      case 2:
        return {
            Lines{spec.firstLine, "", spec.secondLine},
            CursorPos(2, 0),
            "c{" + payload + "<Esc>"};
      case 3:
        return {
            Lines{"ab. cd", "ef."},
            CursorPos(0, clampedIndex(spec.cursorIndex, 6)),
            "c)" + payload + "<Esc>"};
      default:
        return {
            Lines{"ab.", "cd ef."},
            CursorPos(1, clampedIndex(spec.cursorIndex, 6)),
            "c(" + payload + "<Esc>"};
    }
  }

  int lineCol(const string& line, int rawCol) {
    return clampedIndex(rawCol, static_cast<int>(Line(line).effectiveSize()));
  }

  string safeNormalToken(
      const Lines& lines, CursorPos cursor, const MixedTokenSpec& spec) {
    vector<string> choices = {"0", "$", "~", string("r") + spec.replacement};
    const string& line = lines[cursor.line];
    if (cursor.col > 0) {
      choices.push_back("h");
      choices.push_back("X");
    }
    if (cursor.col + 1 < static_cast<int>(line.size())) {
      choices.push_back("l");
    }
    if (line.size() > 1 && cursor.col < static_cast<int>(line.size())) {
      choices.push_back("x");
    }
    return choices[
        clampedIndex(spec.choice, static_cast<int>(choices.size()))];
  }

  void applySequenceToken(Lines& lines, CursorPos& cursor, const string& token) {
    auto [nextLines, nextCursor, mode] =
        applyUserSequence(lines, cursor, token);
    assert(mode == Mode::Normal);
    lines = std::move(nextLines);
    cursor = nextCursor;
  }

  vector<string> movementTokens(const string& seq) {
    auto parsed = parseMovements(seq);
    assert(parsed.has_value());
    vector<string> tokens;
    tokens.reserve(parsed->size());
    for (const ParsedMovement& movement : *parsed) {
      ostringstream out;
      out << movement;
      tokens.push_back(out.str());
    }
    return tokens;
  }

  void expectTokenwiseOracle(
      const Lines& lines,
      CursorPos cursor,
      const string& seq,
      const vector<string>& tokens,
      const Lines& expectedLines,
      CursorPos expectedCursor,
      Mode expectedMode) {
    SCOPED_TRACE(::testing::Message()
                 << "seq='" << seq << "' from " << cursor
                 << "\ninitial=" << lines);
    SimulationResult nvim =
        oracle_.simulateTokens(lines, cursor.line, cursor.col, tokens);

    EXPECT_EQ(nvim.lines, expectedLines);
    EXPECT_EQ(nvim.row, expectedCursor.line);
    EXPECT_EQ(nvim.col, expectedCursor.col);
    EXPECT_EQ(nvim.mode, expectedMode);
  }

  void expectMovementModelMatchesOracle(
      const Lines& lines, CursorPos cursor, const string& seq) {
    CursorPos expected = simulateMovements(cursor, seq, lines);
    expectTokenwiseOracle(
        lines, cursor, seq, movementTokens(seq), lines, expected,
        Mode::Normal);
  }

  void expectSequenceModelMatchesOracle(
      const Lines& lines, CursorPos cursor, const string& seq) {
    auto [expectedLines, expectedCursor, expectedMode] =
        applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq, expectedLines, expectedCursor,
        expectedMode);
  }
};

}  // namespace

FUZZ_TEST_F(InterpreterMatchesOracle, MovementSequences)
    .WithDomains(MovementSequenceSpecDomain());

FUZZ_TEST_F(InterpreterMatchesOracle, CharFindRepeatState)
    .WithDomains(CharFindRepeatSpecDomain());

FUZZ_TEST_F(InterpreterMatchesOracle, EditCommands)
    .WithDomains(EditCommandSpecDomain());

FUZZ_TEST_F(InterpreterMatchesOracle, ParagraphSentenceChangeOperators)
    .WithDomains(ParagraphSentenceChangeSpecDomain());

FUZZ_TEST_F(InterpreterMatchesOracle, MixedNormalModeSequences)
    .WithDomains(MixedNormalSequenceSpecDomain());
