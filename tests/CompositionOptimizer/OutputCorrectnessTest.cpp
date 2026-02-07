// tests/CompositionOptimizer/OutputCorrectnessTest.cpp
//
// Random/stress tests for CompositionOptimizer output correctness.
// These tests use randomly generated buffers and verify results against Neovim.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="CompositionOptimizerOutputCorrectness.*"

#include <gtest/gtest.h>
#include <memory>

#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

class CompositionOptimizerOutputCorrectness : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static const int NUM_ITERATIONS = 30;
  Config config = Config::uniform();
  CompositionOptimizer opt{config};
  CompositionOptimizerParams params{};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  // Verify all results transform initial -> goal via Neovim
  void verifyResults(
      const vector<Result>& results,
      const Lines& initial,
      Position initialPos,
      const Lines& goal,
      const string& testContext = "") {

    for (size_t i = 0; i < results.size(); i++) {
      if (!results[i].isValid()) continue;

      const auto& seq = results[i].getSequenceString();
      SimulationResult nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

      EXPECT_EQ(nvim.lines, goal)
          << "Result " << i << " failed" << (testContext.empty() ? "" : " (" + testContext + ")")
          << "\n  seq='" << seq << "' from " << initialPos
          << "\n  Initial: " << initial
          << "\n  Goal:    " << goal
          << "\n  Got:     " << nvim.lines;
    }
  }
};

unique_ptr<NeovimOracle> CompositionOptimizerOutputCorrectness::oracle;

// =============================================================================
// Single Edit Stress Tests
// =============================================================================

// Random single-line substitutions
TEST_F(CompositionOptimizerOutputCorrectness, SingleLine_Substitution) {
  RandomGen::seed(42);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    // Generate a random line
    int lineLen = RandomGen::range(8, 20);
    string line = randomWord(lineLen);
    Lines initial = {line};

    // Pick a random edit region within the line
    int editStart = RandomGen::range(0, max(0, lineLen - 3));
    int editLen = RandomGen::range(1, min(5, lineLen - editStart));
    int editEnd = editStart + editLen;

    // Generate replacement text (different length ok)
    int replaceLen = RandomGen::range(1, 6);
    string replacement = randomWord(replaceLen);

    // Build goal
    string goalStr = line.substr(0, editStart) + replacement + line.substr(editEnd);
    Lines goal = {goalStr};

    // Random cursor position
    int cursorCol = RandomGen::range(0, max(0, lineLen - 1));
    Position initialPos(0, cursorCol);
    Position goalPos(0, 0);

    auto compResult = opt.optimize(
        initial, initialPos, goal, goalPos, params);
    const auto& results = compResult.results;

    total++;
    if (results.empty()) {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " (no results)\n"
             << "  Initial: '" << line << "'\n"
             << "  Goal:    '" << goalStr << "'" << endl;
      }
      continue;
    }

    // Verify first result
    const auto& seq = results[0].getSequenceString();
    auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

    if (nvim.lines == goal) {
      passed++;
    } else {
      if (total - passed <= 3) {
        // Build visual marker for edit region
        string marker(editStart, ' ');
        marker += string(editEnd - editStart, '^');

        cerr << "FAIL iter=" << iter << " seq='" << seq << "'"
             << " initialPos=(0," << cursorCol << ")\n"
             << "  Initial: '" << line << "'\n"
             << "            " << marker << "\n"
             << "  Edit:     [" << editStart << "," << editEnd << ") '"
             << line.substr(editStart, editLen) << "' -> '" << replacement << "'\n"
             << "  Goal:    '" << goalStr << "'\n"
             << "  Got:     " << nvim.lines << endl;
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// Random multi-line with single edit
TEST_F(CompositionOptimizerOutputCorrectness, MultiLine_SingleEdit) {

  RandomGen::seed(43);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    // Generate random buffer (2-4 lines)
    int numLines = RandomGen::range(2, 4);
    Lines initial = randomLines(numLines, 5, 12);

    // Pick a random line to edit
    int editLine = RandomGen::range(0, numLines - 1);
    string& targetLine = initial[editLine];
    int lineLen = static_cast<int>(targetLine.size());
    if (lineLen < 2) continue;  // Skip very short lines

    // Pick edit region
    int editStart = RandomGen::range(0, max(0, lineLen - 2));
    int editLen = RandomGen::range(1, min(4, lineLen - editStart));
    int editEnd = editStart + editLen;

    // Generate replacement
    string replacement = randomWord(RandomGen::range(1, 5));

    // Build goal
    Lines goal = initial;
    goal[editLine] = targetLine.substr(0, editStart) + replacement + targetLine.substr(editEnd);

    // Random cursor position
    int cursorLine = RandomGen::range(0, numLines - 1);
    int cursorCol = RandomGen::range(0, max(0, static_cast<int>(initial[cursorLine].size()) - 1));
    Position initialPos(cursorLine, cursorCol);
    Position goalPos(0, 0);

    auto compResult = opt.optimize(
        initial, initialPos, goal, goalPos, params);
    const auto& results = compResult.results;

    total++;
    if (results.empty()) {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " (no results)\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << endl;
      }
      continue;
    }

    const auto& seq = results[0].getSequenceString();
    auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

    if (nvim.lines == goal) {
      passed++;
    } else {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " seq='" << seq << "'\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << "\n"
             << "  Got:     " << nvim.lines << endl;
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// =============================================================================
// Pure Insertion/Deletion Stress Tests
// =============================================================================

// Random pure insertions (adding text without removing)
TEST_F(CompositionOptimizerOutputCorrectness, PureInsertion) {

  RandomGen::seed(44);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    // Generate initial buffer
    int numLines = RandomGen::range(1, 3);
    Lines initial = randomLines(numLines, 4, 10);

    // Pick insertion point
    int insertLine = RandomGen::range(0, numLines - 1);
    int lineLen = static_cast<int>(initial[insertLine].size());
    int insertCol = RandomGen::range(0, lineLen);

    // Generate text to insert (no newlines for simplicity)
    string insertText = randomWord(RandomGen::range(2, 6));

    // Build goal
    Lines goal = initial;
    goal[insertLine] = initial[insertLine].substr(0, insertCol) +
                       insertText +
                       initial[insertLine].substr(insertCol);

    // Cursor at beginning
    Position initialPos(0, 0);
    Position goalPos(0, 0);

    auto compResult = opt.optimize(
        initial, initialPos, goal, goalPos, params);
    const auto& results = compResult.results;

    total++;
    if (results.empty()) {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " (no results)\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << endl;
      }
      continue;
    }

    const auto& seq = results[0].getSequenceString();
    auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

    if (nvim.lines == goal) {
      passed++;
    } else {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " seq='" << seq << "'\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << "\n"
             << "  Got:     " << nvim.lines << endl;
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// Random pure deletions (removing text without adding)
TEST_F(CompositionOptimizerOutputCorrectness, PureDeletion) {

  RandomGen::seed(45);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    // Generate initial buffer with enough content to delete
    int numLines = RandomGen::range(1, 3);
    Lines initial = randomLines(numLines, 8, 15);

    // Pick deletion region on a single line
    int deleteLine = RandomGen::range(0, numLines - 1);
    int lineLen = static_cast<int>(initial[deleteLine].size());
    if (lineLen < 4) continue;

    int deleteStart = RandomGen::range(0, lineLen - 3);
    int deleteLen = RandomGen::range(2, min(6, lineLen - deleteStart));
    int deleteEnd = deleteStart + deleteLen;

    // Build goal
    Lines goal = initial;
    goal[deleteLine] = initial[deleteLine].substr(0, deleteStart) +
                       initial[deleteLine].substr(deleteEnd);

    Position initialPos(0, 0);
    Position goalPos(0, 0);

    auto compResult = opt.optimize(
        initial, initialPos, goal, goalPos, params);
    const auto& results = compResult.results;

    total++;
    if (results.empty()) {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " (no results)\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << endl;
      }
      continue;
    }

    const auto& seq = results[0].getSequenceString();
    auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

    if (nvim.lines == goal) {
      passed++;
    } else {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " seq='" << seq << "'\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << "\n"
             << "  Got:     " << nvim.lines << endl;
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// =============================================================================
// Line-Level Operation Tests
// =============================================================================

// Insert new lines
TEST_F(CompositionOptimizerOutputCorrectness, InsertNewLine) {

  RandomGen::seed(46);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    // Start with 2-3 lines
    int numLines = RandomGen::range(2, 3);
    Lines initial = randomLines(numLines, 4, 8);

    // Insert a new line at random position
    int insertAfter = RandomGen::range(0, numLines - 1);
    string newLine = randomWord(RandomGen::range(3, 7));

    Lines goal = initial;
    goal.insert(goal.begin() + insertAfter + 1, newLine);

    Position initialPos(0, 0);
    Position goalPos(0, 0);

    auto compResult = opt.optimize(
        initial, initialPos, goal, goalPos, params);
    const auto& results = compResult.results;

    total++;
    if (results.empty()) {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " (no results)\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << endl;
      }
      continue;
    }

    const auto& seq = results[0].getSequenceString();
    auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

    if (nvim.lines == goal) {
      passed++;
    } else {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " seq='" << seq << "'\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << "\n"
             << "  Got:     " << nvim.lines << endl;
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// Delete entire lines
TEST_F(CompositionOptimizerOutputCorrectness, DeleteEntireLine) {

  RandomGen::seed(47);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    // Start with 3-4 lines
    int numLines = RandomGen::range(3, 4);
    Lines initial = randomLines(numLines, 4, 8);

    // Delete a middle line
    int deleteLine = RandomGen::range(1, numLines - 2);

    Lines goal = initial;
    goal.erase(goal.begin() + deleteLine);

    Position initialPos(0, 0);
    Position goalPos(0, 0);

    auto compResult = opt.optimize(
        initial, initialPos, goal, goalPos, params);
    const auto& results = compResult.results;

    total++;
    if (results.empty()) {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " (no results)\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << endl;
      }
      continue;
    }

    const auto& seq = results[0].getSequenceString();
    auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

    if (nvim.lines == goal) {
      passed++;
    } else {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " seq='" << seq << "'\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << "\n"
             << "  Got:     " << nvim.lines << endl;
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// =============================================================================
// Multiple Edit Tests
// =============================================================================

// Two edits on the same line
TEST_F(CompositionOptimizerOutputCorrectness, TwoEdits_SameLine) {

  RandomGen::seed(48);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    // Generate a line with distinct "words" separated by spaces
    string line = randomWord(3) + " " + randomWord(4) + " " + randomWord(3);
    Lines initial = {line};

    // Replace first and last word
    string newFirst = randomWord(3);
    string newLast = randomWord(3);

    // Build goal by finding word boundaries
    size_t firstSpace = line.find(' ');
    size_t lastSpace = line.rfind(' ');
    string middle = line.substr(firstSpace, lastSpace - firstSpace + 1);
    string goalStr = newFirst + middle + newLast;
    Lines goal = {goalStr};

    Position initialPos(0, 0);
    Position goalPos(0, 0);

    auto compResult = opt.optimize(
        initial, initialPos, goal, goalPos, params);
    const auto& results = compResult.results;

    total++;
    if (results.empty()) {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " (no results)\n"
             << "  Initial: '" << line << "'\n"
             << "  Goal:    '" << goalStr << "'" << endl;
      }
      continue;
    }

    const auto& seq = results[0].getSequenceString();
    auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

    if (nvim.lines == goal) {
      passed++;
    } else {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " seq='" << seq << "'\n"
             << "  Initial: '" << line << "'\n"
             << "  Goal:    '" << goalStr << "'\n"
             << "  Got:     " << nvim.lines << endl;
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}

// Two edits on different lines
TEST_F(CompositionOptimizerOutputCorrectness, TwoEdits_DifferentLines) {

  RandomGen::seed(49);
  int passed = 0, total = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    // Generate 3 lines
    Lines initial = randomLines(3, 5, 10);

    // Edit first and last line (keeping middle unchanged)
    Lines goal = initial;
    goal[0] = randomWord(RandomGen::range(4, 8));
    goal[2] = randomWord(RandomGen::range(4, 8));

    Position initialPos(0, 0);
    Position goalPos(0, 0);

    auto compResult = opt.optimize(
        initial, initialPos, goal, goalPos, params);
    const auto& results = compResult.results;

    total++;
    if (results.empty()) {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " (no results)\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << endl;
      }
      continue;
    }

    const auto& seq = results[0].getSequenceString();
    auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, seq.keys);

    if (nvim.lines == goal) {
      passed++;
    } else {
      if (total - passed <= 3) {
        cerr << "FAIL iter=" << iter << " seq='" << seq << "'\n"
             << "  Initial: " << initial << "\n"
             << "  Goal:    " << goal << "\n"
             << "  Got:     " << nvim.lines << endl;
      }
    }
  }

  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";
}
