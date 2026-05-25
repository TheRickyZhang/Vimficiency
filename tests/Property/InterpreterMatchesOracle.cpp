// Property: generated Vim input sequences must leave our interpreter in the
// same buffer/cursor/mode state as Neovim. Focused generators cover movement,
// char-find repeat state, edits, and small mixed normal-mode sequences.

#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MovementInterpreter.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/InterpreterModelReplay.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"
#include "Property/PropertyTestUtils.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

string randomOptionalCount(int maxCount) {
  if (RandomGen::range(0, 3) != 0) return "";
  return to_string(RandomGen::range(2, maxCount));
}

char randomFindCommand() {
  static constexpr char cmds[] = {'f', 'F', 't', 'T'};
  return cmds[RandomGen::range(0, 3)];
}

char randomFindTarget(const string& line) {
  if (!line.empty() && RandomGen::range(0, 1) == 0) {
    return line[randomCol(line)];
  }
  return static_cast<char>('a' + RandomGen::range(0, 25));
}

char randomFindTarget(const Lines& lines) {
  const string& line = lines[randomLineIndex(lines)];
  if (!line.empty() && RandomGen::range(0, 1) == 0) {
    char c = line[randomCol(line)];
    if (c != '<') return c;
  }
  static constexpr string_view targets = "abcdef .,";
  return targets[randomCol(targets)];
}

string randomCharFindMotion(const Lines& lines, int maxCount) {
  string seq = randomOptionalCount(maxCount);
  seq += randomFindCommand();
  seq += randomFindTarget(lines);
  return seq;
}

class InterpreterMatchesOracle {
 public:
  void MovementSequences(uint32_t seed) {
    runSeedDriverCases(seed, 30, [&] {
      Lines lines = randomProseBuffer(RandomGen::range(1, 8));
      CursorPos cursor = randomPos(lines);
      string seq = randomMovementSequence(lines, RandomGen::range(1, 12));

      expectMovementModelMatchesOracle(lines, cursor, seq);
    });
  }

  void CharFindRepeatState(uint32_t seed) {
    runSeedDriverCases(seed, 40, [&] {
      Lines lines{randomLine(RandomGen::range(2, 24))};
      const string& line = lines[0];
      CursorPos cursor(0, randomCol(line));

      string seq;
      seq += randomFindCommand();
      seq += randomFindTarget(line);
      seq += randomRepeatChain(RandomGen::range(0, 4));

      expectMovementModelMatchesOracle(lines, cursor, seq);
    });
  }

  void EditCommands(uint32_t seed) {
    runSeedDriverCases(seed, 40, [&] {
      auto test = randomEditCommandCase();
      expectSequenceModelMatchesOracle(test.initial, test.cursor, test.seq);
    });
  }

  void ParagraphSentenceChangeOperators(uint32_t seed) {
    runSeedDriverCases(seed, 50, [&] {
      auto test = randomParagraphSentenceChangeCase();
      expectSequenceModelMatchesOracle(test.initial, test.cursor, test.seq);
    });
  }

  void MixedNormalModeSequences(uint32_t seed) {
    runSeedDriverCases(seed, 30, [&] {
      Lines lines{randomWord(RandomGen::range(12, 24))};
      CursorPos cursor = randomPos(lines);
      Lines initial = lines;
      CursorPos initialCursor = cursor;
      string seq;

      int tokenCount = RandomGen::range(3, 10);
      for (int i = 0; i < tokenCount; i++) {
        string token = randomSafeNormalToken(lines, cursor);
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
    });
  }

 private:
  struct EditCommandCase {
    Lines initial;
    CursorPos cursor;
    string seq;
  };

  NeovimOracle oracle_{};

  string randomMovementSequence(const Lines& lines, int tokenCount) {
    string seq;
    bool hasFindState = false;
    for (int i = 0; i < tokenCount; i++) {
      int choice = RandomGen::range(0, 99);
      if (choice < 20) {
        seq += randomCharFindMotion(lines, /*maxCount=*/4);
        hasFindState = true;
      } else if (hasFindState && choice < 35) {
        seq += randomOptionalCount(/*maxCount=*/4);
        seq += RandomGen::range(0, 1) == 0 ? ";" : ",";
      } else {
        seq += randomBasicMotion();
      }
    }
    return seq;
  }

  string randomBasicMotion() {
    static constexpr array<string_view, 15> countable = {
        "h", "l", "j", "k", "$", "gg", "G",
        "w", "W", "b", "B", "e", "E", "ge", "gE",
    };
    static constexpr array<string_view, 2> uncounted = {"0", "^"};

    if (RandomGen::range(0, 9) < 8) {
      return randomOptionalCount(/*maxCount=*/6) +
             string(RandomGen::pick(countable));
    }
    return string(RandomGen::pick(uncounted));
  }

  string randomRepeatChain(int length) {
    string chain;
    for (int i = 0; i < length; i++) {
      chain += (RandomGen::range(0, 1) == 0) ? ';' : ',';
    }
    return chain;
  }

  EditCommandCase randomEditCommandCase() {
    switch (RandomGen::range(0, 8)) {
      case 0:
        return {Lines{"abcdef"}, CursorPos(0, RandomGen::range(0, 5)), "x"};
      case 1:
        return {Lines{"abcdef"}, CursorPos(0, RandomGen::range(1, 5)), "X"};
      case 2:
        return {Lines{"abcdef"}, CursorPos(0, RandomGen::range(0, 5)),
                string("r") + randomWord(1)};
      case 3:
        return {Lines{"abcdef"}, CursorPos(0, RandomGen::range(0, 5)), "~"};
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

  EditCommandCase randomParagraphSentenceChangeCase() {
    string payload = randomWord(RandomGen::range(1, 5));
    switch (RandomGen::range(0, 4)) {
      case 0:
        return {
            Lines{randomLine(RandomGen::range(3, 6)), randomLine(RandomGen::range(3, 6))},
            CursorPos(RandomGen::range(0, 1), 0),
            "c}" + payload + "<Esc>"};
      case 1:
        return {
            Lines{randomLine(RandomGen::range(3, 6)), "", randomLine(RandomGen::range(3, 6))},
            CursorPos(0, RandomGen::range(0, 2)),
            "c}" + payload + "<Esc>"};
      case 2:
        return {
            Lines{randomLine(RandomGen::range(3, 6)), "", randomLine(RandomGen::range(3, 6))},
            CursorPos(2, 0),
            "c{" + payload + "<Esc>"};
      case 3:
        return {
            Lines{"ab. cd", "ef."},
            CursorPos(0, RandomGen::range(0, 5)),
            "c)" + payload + "<Esc>"};
      default:
        return {
            Lines{"ab.", "cd ef."},
            CursorPos(1, RandomGen::range(0, 5)),
            "c(" + payload + "<Esc>"};
    }
  }

  string randomSafeNormalToken(const Lines& lines, CursorPos cursor) {
    vector<string> choices = {"0", "$", "~", string("r") + randomWord(1)};
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
    return RandomGen::pick(choices);
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
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000));

FUZZ_TEST_F(InterpreterMatchesOracle, CharFindRepeatState)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000));

FUZZ_TEST_F(InterpreterMatchesOracle, EditCommands)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000));

FUZZ_TEST_F(InterpreterMatchesOracle, ParagraphSentenceChangeOperators)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000));

FUZZ_TEST_F(InterpreterMatchesOracle, MixedNormalModeSequences)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000));
