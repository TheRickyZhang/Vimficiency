#include <algorithm>
#include <cstdint>
#include <exception>
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

// Character-find motion conformance against Neovim. Covers single find motions
// (f/F/t/T) and arbitrary chains of `;` (repeat last find) and `,` (reverse
// last find). Char-find motions stay on the same line in Vim, so a single-line
// buffer is sufficient and keeps the oracle interaction cheap.
class CharFindMotionGeneratedPropertyTest {
 public:
  void FindMotionsMatchOracle(uint32_t seed) {
    runCases(seed, 40, [&] {
      Lines lines{randomLine(RandomGen::range(2, 24))};
      const string& line = lines[0];
      int col = RandomGen::range(0, max(0, static_cast<int>(line.size()) - 1));
      char target = pickTarget(line);
      char findCmd = pickFindCommand();
      string seq;
      seq += findCmd;
      seq += target;
      seq += randomRepeatChain(RandomGen::range(0, 4));

      expectMatchesOracle(lines, col, seq);
    });
  }

 private:
  NeovimOracle oracle_{};

  template <typename Fn>
  void runCases(uint32_t seed, int count, Fn&& fn) {
    RandomGen::seed(seed);
    for (int caseIndex = 0; caseIndex < count; caseIndex++) {
      SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
      fn();
    }
  }

  // 50% of the time return a char actually present in the line (so the motion
  // succeeds); 50% a random printable letter, which may not be present and
  // tests the no-op-on-miss branch.
  char pickTarget(const string& line) {
    if (!line.empty() && RandomGen::range(0, 1) == 0) {
      return line[RandomGen::range(0, static_cast<int>(line.size()) - 1)];
    }
    return static_cast<char>('a' + RandomGen::range(0, 25));
  }

  char pickFindCommand() {
    static constexpr char cmds[] = {'f', 'F', 't', 'T'};
    return cmds[RandomGen::range(0, 3)];
  }

  string randomRepeatChain(int length) {
    string chain;
    for (int i = 0; i < length; i++) {
      chain += (RandomGen::range(0, 1) == 0) ? ';' : ',';
    }
    return chain;
  }

  void expectMatchesOracle(const Lines& lines, int col, const string& seq) {
    SCOPED_TRACE(::testing::Message()
                 << "line='" << (lines.empty() ? "" : lines[0]) << "'"
                 << " col=" << col << " seq='" << seq << "'");

    SimulationResult oracleResult;
    try {
      oracleResult = oracle_.simulate(lines, 0, col, seq);
    } catch (const exception& e) {
      oracle_.restart();
      FAIL() << "NeovimOracle failed for seq='" << seq << "': " << e.what();
    }

    CursorPos ours = simulateMovements(CursorPos(0, col), seq, lines);
    EXPECT_EQ(ours.line, oracleResult.row);
    EXPECT_EQ(ours.col, oracleResult.col);
  }
};

}  // namespace

FUZZ_TEST_F(CharFindMotionGeneratedPropertyTest, FindMotionsMatchOracle)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({3001});
