#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/MovementInterpreter.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

// Counted motion conformance against Neovim. Two flavors:
//   1. {count}{motion} for plain motions (h/l/j/k/gg/G/$/w/W/b/B/e/E/ge/gE)
//   2. {count}f{c} / {count}F{c} / {count}t{c} / {count}T{c}
// Counted operator+motion ({count}{op}{motion}, e.g. d3w) is intentionally
// not yet covered here — it exercises the optimizer's counted-edit path,
// which has its own coverage in tests/Properties/TransformOptimizerProperties.cpp.
class CountedMotionGeneratedPropertyTest {
 public:
  void CountedBasicMotionsMatchOracle(uint32_t seed) {
    runCases(seed, 30, [&] {
      auto [lines, cursor] = generateBufferAndCursor(/*minLines=*/3, /*maxLines=*/8);
      int count = RandomGen::range(1, 12);
      string motion = pickBasicMotion();
      string seq = to_string(count) + motion;
      expectMatchesOracle(lines, cursor, seq);
    });
  }

  void CountedCharFindMatchesOracle(uint32_t seed) {
    runCases(seed, 30, [&] {
      Lines lines{randomLine(RandomGen::range(8, 30))};
      const string& line = lines[0];
      int col = RandomGen::range(0, max(0, static_cast<int>(line.size()) - 1));
      int count = RandomGen::range(1, 5);
      char findCmd = pickFindCommand();
      char target = pickTarget(line);
      string seq = to_string(count) + findCmd + target;
      expectMatchesOracle(lines, CursorPos(0, col), seq);
    });
  }

 private:
  unique_ptr<NeovimOracle> oracle_{make_unique<NeovimOracle>()};

  template <typename Fn>
  void runCases(uint32_t seed, int count, Fn&& fn) {
    RandomGen::seed(seed);
    for (int caseIndex = 0; caseIndex < count; caseIndex++) {
      SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
      fn();
    }
  }

  pair<Lines, CursorPos> generateBufferAndCursor(int minLines, int maxLines) {
    int lineCount = RandomGen::range(minLines, maxLines);
    Lines lines = randomProseLines(lineCount, 6, 24);
    int line = RandomGen::range(0, lineCount - 1);
    int maxCol = lines[line].empty()
        ? 0
        : static_cast<int>(lines[line].size()) - 1;
    int col = RandomGen::range(0, max(0, maxCol));
    return {std::move(lines), CursorPos(line, col)};
  }

  string pickBasicMotion() {
    static const string motions[] = {
        "h", "l", "j", "k", "gg", "G", "$",
        "w", "W", "b", "B", "e", "E", "ge", "gE",
    };
    constexpr int n = sizeof(motions) / sizeof(motions[0]);
    return motions[RandomGen::range(0, n - 1)];
  }

  char pickFindCommand() {
    static constexpr char cmds[] = {'f', 'F', 't', 'T'};
    return cmds[RandomGen::range(0, 3)];
  }

  char pickTarget(const string& line) {
    if (!line.empty() && RandomGen::range(0, 1) == 0) {
      return line[RandomGen::range(0, static_cast<int>(line.size()) - 1)];
    }
    return static_cast<char>('a' + RandomGen::range(0, 25));
  }

  void expectMatchesOracle(const Lines& lines, CursorPos cursor, const string& seq) {
    SCOPED_TRACE(::testing::Message()
                 << "cursor=(" << cursor.line << "," << cursor.col << ")"
                 << " seq='" << seq << "'"
                 << " lines=" << lines);

    SimulationResult oracleResult;
    try {
      oracleResult = oracle_->simulate(lines, cursor.line, cursor.col, seq);
    } catch (const exception& e) {
      oracle_->restart();
      FAIL() << "NeovimOracle failed for seq='" << seq << "': " << e.what();
    }

    CursorPos ours = simulateMovements(cursor, seq, lines);
    EXPECT_EQ(ours.line, oracleResult.row);
    EXPECT_EQ(ours.col, oracleResult.col);
  }
};

}  // namespace

FUZZ_TEST_F(CountedMotionGeneratedPropertyTest, CountedBasicMotionsMatchOracle)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({5001});

FUZZ_TEST_F(CountedMotionGeneratedPropertyTest, CountedCharFindMatchesOracle)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({5002});
