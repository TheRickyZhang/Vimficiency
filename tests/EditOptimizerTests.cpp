// tests/EditOptimizerTests.cpp
//
// Tests for EditOptimizer - verifies edit sequence optimization for text transformations.
//
// NOTE: EditState::applySingleMotion is currently a stub (see State/EditState.cpp).
// It only appends to motionSequence but doesn't actually apply edits to the buffer.
// This means the search cannot reach different goal states.
//
// Tests are organized into:
// - Working tests: Structure, dimensions, identical input (no edits needed)
// - Pending tests: Require applySingleMotion implementation (marked DISABLED)

#include <gtest/gtest.h>

#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Optimizer/EditBoundary.h"
#include "Utils/Debug.h"
#include "Utils/Lines.h"

using namespace std;

class EditOptimizerTest : public ::testing::Test {
protected:
  Config config = Config::uniform();

  EditOptimizer makeOptimizer() {
    return EditOptimizer(config, 1e5, 1.0, 2.0, 3.0);
  }

  // All flags false = boundaries don't cut through words, so all edits allowed
  EditBoundary noBoundary() {
    return EditBoundary{};  // Default: all false
  }

  // Count valid results in EditResult
  int countValidResults(const EditResult& res) {
    int count = 0;
    for (int i = 0; i < res.n; i++) {
      for (int j = 0; j < res.m; j++) {
        if (res.adj[i][j].isValid()) count++;
      }
    }
    return count;
  }

  // Count results where i == j (diagonal - same position start/end)
  int countSamePosResults(const EditResult& res) {
    int count = 0;
    int minDim = min(res.n, res.m);
    for (int i = 0; i < minDim; i++) {
      if (res.adj[i][i].isValid() < 1e9) count++;
    }
    return count;
  }

  void printEditResult(const EditResult& res, const Lines& beginLines, const Lines& endLines) {
    cout << "EditResult (" << res.n << " x " << res.m << "):" << endl;
    cout << "Begin: ";
    for (const auto& l : beginLines) cout << "\"" << l << "\" ";
    cout << endl;
    cout << "End: ";
    for (const auto& l : endLines) cout << "\"" << l << "\" ";
    cout << endl;

    for (int i = 0; i < res.n; i++) {
      for (int j = 0; j < res.m; j++) {
        if (res.adj[i][j].isValid()) {
          cout << "  [" << i << "][" << j << "] = \"" << res.adj[i][j].sequence
               << "\" (cost: " << res.adj[i][j].keyCost << ")" << endl;
        }
      }
    }
  }
};


TEST_F(EditOptimizerTest, MatrixDimensions_SingleLine) {
  Lines begin = {"abcde"};  // 5 chars
  Lines end = {"xyz"};      // 3 chars

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  printEditResult(res, begin, end);

  EXPECT_EQ(res.n, 5) << "n should equal total chars in begin";
  EXPECT_EQ(res.m, 3) << "m should equal total chars in end";
}

TEST_F(EditOptimizerTest, MatrixDimensions_MultiLine) {
  Lines begin = {"ab", "cd"};  // 4 chars total (2+2)
  Lines end = {"efg"};         // 3 chars

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  EXPECT_EQ(res.n, 4) << "n should equal total chars in begin lines";
  EXPECT_EQ(res.m, 3) << "m should equal total chars in end lines";
}

TEST_F(EditOptimizerTest, MatrixDimensions_MultiLineToMultiLine) {
  Lines begin = {"aa", "bb", "cc"};  // 6 chars
  Lines end = {"xx", "yy"};          // 4 chars

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  EXPECT_EQ(res.n, 6);
  EXPECT_EQ(res.m, 4);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(EditOptimizerTest, EmptyToNonEmpty) {
  // Pure insertion from empty - degenerate case
  Lines begin = {""};  // Empty line (0 chars)
  Lines end = {"a"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  // n=0 means no starting positions
  EXPECT_EQ(res.n, 0);
  EXPECT_EQ(res.m, 1);
}

TEST_F(EditOptimizerTest, NonEmptyToEmpty) {
  // Complete deletion
  Lines begin = {"a"};
  Lines end = {""};

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  // m=0 means no ending positions
  EXPECT_EQ(res.n, 1);
  EXPECT_EQ(res.m, 0);
}

// =============================================================================
// DISABLED Tests - Require EditState::applySingleMotion implementation
// =============================================================================
// These tests are disabled until motion application is implemented.
// They verify actual edit optimization (deletion, insertion, substitution).

TEST_F(EditOptimizerTest, SingleCharDeletion) {
  // Delete single character: "ab" -> "a"
  Lines begin = {"ab"};
  Lines end = {"a"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  cout << get_debug_output() << endl;
  printEditResult(res, begin, end);

  EXPECT_GT(countValidResults(res), 0) << "Should find at least one edit path";
  EXPECT_EQ(res.n, 2);
  EXPECT_EQ(res.m, 1);
}

// Cannot insert - would need to type actual characters (infinite search space)
TEST_F(EditOptimizerTest, DISABLED_SingleCharInsertion) {
  // Insert single character: "a" -> "ab"
  Lines begin = {"a"};
  Lines end = {"ab"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  EXPECT_GT(countValidResults(res), 0) << "Should find at least one edit path";
  EXPECT_EQ(res.n, 1);
  EXPECT_EQ(res.m, 2);
}

// Cannot substitute - r{char} requires typing replacement character
TEST_F(EditOptimizerTest, DISABLED_SingleCharSubstitution) {
  // Substitute: "a" -> "b"
  Lines begin = {"a"};
  Lines end = {"b"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  EXPECT_GT(countValidResults(res), 0) << "Should find at least one edit path";
  EXPECT_EQ(res.n, 1);
  EXPECT_EQ(res.m, 1);
}

TEST_F(EditOptimizerTest, WordDeletion) {
  // Delete word: "hello world" -> "hello"
  Lines begin = {"hello world"};
  Lines end = {"hello"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  EXPECT_GT(countValidResults(res), 0) << "Should find at least one edit path";
}

TEST_F(EditOptimizerTest, DISABLED_MultiLineToSingleLine) {
  // Join lines: ["hello", "world"] -> ["hello world"]
  Lines begin = {"hello", "world"};
  Lines end = {"hello world"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = opt.optimizeEdit(begin, end, noBoundary());

  EXPECT_EQ(res.n, 10);
  EXPECT_EQ(res.m, 11);
  EXPECT_GT(countValidResults(res), 0) << "Should find at least one edit path";
}

