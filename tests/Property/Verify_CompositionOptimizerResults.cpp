// Property: for generated before/after buffers, CompositionOptimizer top results
// must replay in Neovim from the generated initial cursor to the exact generated
// goal buffer and goal cursor.

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Property/OptimizerResultChecks.h"
#include "Property/PropertyDomains.h"

using namespace std;

namespace {

int clampedIndex(int value, int size) {
  return std::clamp(value, 0, size - 1);
}

int clampedInsertIndex(int value, int size) {
  return std::clamp(value, 0, size);
}

struct CompositionReplayCase {
  Lines initial;
  CursorPos initialPos;
  Lines goal;
  CursorPos goalPos;
};

struct CompositionMutationSpec {
  int kind;
  int lineIndex;
  int beginCol;
  int endCol;
  string text;
};

struct CompositionReplayCaseSpec {
  vector<string> initial;
  vector<CompositionMutationSpec> mutations;
  int initialPosIndex;
  int goalPosIndex;
};

void replaceLineSpan(Lines& lines, const CompositionMutationSpec& mutation) {
  int line = clampedIndex(mutation.lineIndex, static_cast<int>(lines.size()));
  string& text = lines[line];
  int begin = std::clamp(mutation.beginCol, 0, static_cast<int>(text.size()));
  int end = std::clamp(mutation.endCol, begin, static_cast<int>(text.size()));
  text.replace(begin, end - begin, mutation.text);
}

void splitLine(Lines& lines, const CompositionMutationSpec& mutation) {
  int line = clampedIndex(mutation.lineIndex, static_cast<int>(lines.size()));
  string& text = lines[line];
  int col = std::clamp(mutation.beginCol, 0, static_cast<int>(text.size()));
  string suffix = text.substr(col);
  text.erase(col);
  lines.insert(lines.begin() + line + 1, suffix);
}

void joinAdjacentLines(Lines& lines, const CompositionMutationSpec& mutation) {
  if (lines.size() < 2) {
    replaceLineSpan(lines, mutation);
    return;
  }

  int line = clampedIndex(mutation.lineIndex, lines.lastLine());
  lines[line] += lines[line + 1];
  lines.erase(lines.begin() + line + 1);
}

void insertLine(Lines& lines, const CompositionMutationSpec& mutation) {
  int index =
      clampedInsertIndex(mutation.lineIndex, static_cast<int>(lines.size()));
  lines.insert(lines.begin() + index, mutation.text);
}

void deleteLine(Lines& lines, const CompositionMutationSpec& mutation) {
  if (lines.size() < 2) {
    replaceLineSpan(lines, mutation);
    return;
  }

  int line = clampedIndex(mutation.lineIndex, static_cast<int>(lines.size()));
  lines.erase(lines.begin() + line);
}

void applyMutation(Lines& lines, const CompositionMutationSpec& mutation) {
  switch (mutation.kind) {
    case 0:
      replaceLineSpan(lines, mutation);
      break;
    case 1:
      splitLine(lines, mutation);
      break;
    case 2:
      joinAdjacentLines(lines, mutation);
      break;
    case 3:
      insertLine(lines, mutation);
      break;
    default:
      deleteLine(lines, mutation);
      break;
  }
}

CompositionReplayCase toReplayCase(const CompositionReplayCaseSpec& spec) {
  CompositionReplayCase test;
  test.initial = Lines(spec.initial);
  test.goal = test.initial;

  for (const auto& mutation : spec.mutations) {
    applyMutation(test.goal, mutation);
  }

  test.initialPos = test.initial.cursorFromFlatIndexClamped(spec.initialPosIndex);
  test.goalPos = test.goal.cursorFromFlatIndexClamped(spec.goalPosIndex);
  return test;
}

string formatMutation(const CompositionMutationSpec& mutation) {
  ostringstream out;
  out << "{kind=" << mutation.kind
      << ", lineIndex=" << mutation.lineIndex
      << ", beginCol=" << mutation.beginCol
      << ", endCol=" << mutation.endCol
      << ", text='" << mutation.text << "'}";
  return out.str();
}

string formatMutations(const vector<CompositionMutationSpec>& mutations) {
  string out;
  for (const auto& mutation : mutations) {
    if (!out.empty()) out += ", ";
    out += formatMutation(mutation);
  }
  return out;
}

auto CompositionMutationSpecDomain() {
  return fuzztest::StructOf<CompositionMutationSpec>(
      fuzztest::InRange<int>(0, 4),
      fuzztest::InRange<int>(0, 6),
      fuzztest::InRange<int>(0, 12),
      fuzztest::InRange<int>(0, 12),
      PropertyDomains::LineTextDomain(0, 6));
}

auto CompositionReplayCaseSpecDomain() {
  return fuzztest::StructOf<CompositionReplayCaseSpec>(
      PropertyDomains::LineVecDomain(1, 4, 0, 9),
      fuzztest::VectorOf(CompositionMutationSpecDomain())
          .WithMinSize(1)
          .WithMaxSize(3),
      fuzztest::InRange<int>(0, 64),
      fuzztest::InRange<int>(0, 64));
}

class CompositionOptimizerGeneratedPropertyTest {
 public:
  void ShrinkableBufferMutationsTopResultsReplay(
      const CompositionReplayCaseSpec& spec) {
    CompositionReplayCase test = toReplayCase(spec);
    if (test.initial == test.goal) return;

    auto compResult = opt_.optimize(
        test.initial, test.initialPos, test.goal, test.goalPos, params_);

    ostringstream context;
    context << "shrinkable composition mutation"
            << " initial=" << test.initial
            << " initialPos=" << test.initialPos
            << " goal=" << test.goal
            << " goalPos=" << test.goalPos
            << " rawInitialPosIndex=" << spec.initialPosIndex
            << " rawGoalPosIndex=" << spec.goalPosIndex
            << " mutations=[" << formatMutations(spec.mutations) << "]";
    expectTopResultsReplay(
        oracle_, compResult.getResults(), test.initial, test.initialPos,
        test.goal, MAX_RESULTS_TO_REPLAY, context.str(), test.goalPos);
  }

 private:
  static constexpr size_t MAX_RESULTS_TO_REPLAY = 3;

  Config config_ = Config::uniform();
  CompositionOptimizer opt_{config_};
  CompositionOptimizerParams params_{};
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(
    CompositionOptimizerGeneratedPropertyTest,
    ShrinkableBufferMutationsTopResultsReplay)
    .WithDomains(CompositionReplayCaseSpecDomain());
