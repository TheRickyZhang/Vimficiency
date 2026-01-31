// tests/Benchmarks/MotionTemplateSpecializationBench.cpp
//
// A/B benchmarks comparing templated vs runtime-dispatch implementations.
// Measures the performance difference from compile-time dispatch.
//
// Run with: ./vimficiency_benchmarks --gtest_filter="*TemplateSpecialization*"

#include <gtest/gtest.h>

#include "AlternateImplementations.h"
#include "BenchUtils.h"
#include "Utils/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "VimCore/EdgeType.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEndpointUtils.h"

using namespace std;

class MotionTemplateSpecializationBench : public ::testing::Test {
protected:
  static void SetUpTestSuite() { RandomGen::seed(42); }

  // Generate test cases: buffer + starting positions + motion params
  struct MotionTestCase {
    Lines lines;
    Position cursor;
    bool forward;
    EdgeType edgeType;
    bool big;
    bool skipCurrent;
  };

  static vector<MotionTestCase> generateTestCases(int count) {
    vector<MotionTestCase> cases;
    cases.reserve(count);

    for (int i = 0; i < count; i++) {
      // Vary buffer size
      int numLines = 5 + (i % 20);
      int avgLineLen = 20 + (i % 40);
      Lines lines = randomProseLines(numLines, avgLineLen, avgLineLen + 10);

      // Random position within buffer
      int line = RandomGen::range(0, static_cast<int>(lines.size()) - 1);
      int col = lines[line].empty() ? 0 : RandomGen::range(0, static_cast<int>(lines[line].size()) - 1);
      Position cursor(line, col);

      // Random motion params
      bool forward = RandomGen::chance(1, 2);
      EdgeType edgeType = static_cast<EdgeType>(RandomGen::range(0, 2));
      bool big = RandomGen::chance(1, 2);
      bool skipCurrent = RandomGen::chance(1, 2);

      cases.push_back({lines, cursor, forward, edgeType, big, skipCurrent});
    }

    return cases;
  }

  // Benchmark result with verification
  struct ComparisonResult {
    TimingStats templatedTiming;
    TimingStats runtimeTiming;
    int mismatchCount;  // Should be 0 if implementations are equivalent
    double speedupRatio;  // templated time / runtime time (< 1.0 means templated is faster)
  };
};

// =============================================================================
// motionWordCore: Templated vs Runtime
// =============================================================================

TEST_F(MotionTemplateSpecializationBench, DISABLED_MotionWordCore) {
  printHeader("motionWordCore: Templated vs Runtime Dispatch");

  const int NUM_CASES = 1000;
  const int ITERATIONS = 100;

  auto testCases = generateTestCases(NUM_CASES);

  // Verify equivalence first
  int mismatches = 0;
  for (const auto& tc : testCases) {
    Position templatedResult;
    Position runtimeResult;

    // Call templated version via runtime dispatcher (which calls template)
    templatedResult = VimCore::motionWordCore(
        tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent);

    // Call runtime-dispatch version
    runtimeResult = Alternate::motionWordCoreRuntime(
        tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent);

    if (templatedResult != runtimeResult) {
      mismatches++;
      if (mismatches <= 3) {  // Print first few mismatches
        cerr << "Mismatch: forward=" << tc.forward << " edge=" << static_cast<int>(tc.edgeType)
             << " big=" << tc.big << " skip=" << tc.skipCurrent
             << " templated=" << templatedResult.line << "," << templatedResult.col
             << " runtime=" << runtimeResult.line << "," << runtimeResult.col << endl;
      }
    }
  }

  cout << "Equivalence check: " << (NUM_CASES - mismatches) << "/" << NUM_CASES << " match" << endl;
  if (mismatches > 0) {
    cout << "WARNING: " << mismatches << " mismatches detected!" << endl;
  }

  // Benchmark templated (via runtime dispatcher that calls templates)
  TimingStats templatedTiming = measureTiming([&]() {
    for (const auto& tc : testCases) {
      volatile Position result = VimCore::motionWordCore(
          tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent);
      (void)result;
    }
  }, ITERATIONS);

  // Benchmark runtime-dispatch version
  TimingStats runtimeTiming = measureTiming([&]() {
    for (const auto& tc : testCases) {
      volatile Position result = Alternate::motionWordCoreRuntime(
          tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent);
      (void)result;
    }
  }, ITERATIONS);

  cout << "\nResults (" << NUM_CASES << " cases, " << ITERATIONS << " iterations):" << endl;
  printTableHeader("Version");
  printRow("Templated", templatedTiming);
  printRow("Runtime", runtimeTiming);

  double speedup = runtimeTiming.avgMs / templatedTiming.avgMs;
  cout << "\nSpeedup ratio: " << fixed << setprecision(2) << speedup << "x" << endl;
  if (speedup > 1.0) {
    cout << "  -> Templated version is " << ((speedup - 1.0) * 100) << "% faster" << endl;
  } else {
    cout << "  -> Runtime version is " << ((1.0/speedup - 1.0) * 100) << "% faster" << endl;
  }
}

// =============================================================================
// motionWordEndpoint: Templated vs Runtime
// =============================================================================

TEST_F(MotionTemplateSpecializationBench, DISABLED_MotionWordEndpoint) {
  printHeader("motionWordEndpoint: Templated vs Runtime Dispatch");

  const int NUM_CASES = 1000;
  const int ITERATIONS = 100;

  auto testCases = generateTestCases(NUM_CASES);

  // Add boundary params
  const int boundaryOffset = 5;
  const bool hasLinesOutside = true;

  // Verify equivalence
  int mismatches = 0;
  for (const auto& tc : testCases) {
    Position templatedResult = VimCore::motionWordEndpoint(
        tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent,
        boundaryOffset, hasLinesOutside, /*lineBounded=*/false);

    Position runtimeResult = Alternate::motionWordEndpointRuntime(
        tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent,
        boundaryOffset, hasLinesOutside, /*lineBounded=*/false);

    if (templatedResult != runtimeResult) {
      mismatches++;
    }
  }

  cout << "Equivalence check: " << (NUM_CASES - mismatches) << "/" << NUM_CASES << " match" << endl;

  // Benchmark templated
  TimingStats templatedTiming = measureTiming([&]() {
    for (const auto& tc : testCases) {
      volatile Position result = VimCore::motionWordEndpoint(
          tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent,
          boundaryOffset, hasLinesOutside, /*lineBounded=*/false);
      (void)result;
    }
  }, ITERATIONS);

  // Benchmark runtime
  TimingStats runtimeTiming = measureTiming([&]() {
    for (const auto& tc : testCases) {
      volatile Position result = Alternate::motionWordEndpointRuntime(
          tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent,
          boundaryOffset, hasLinesOutside, /*lineBounded=*/false);
      (void)result;
    }
  }, ITERATIONS);

  cout << "\nResults (" << NUM_CASES << " cases, " << ITERATIONS << " iterations):" << endl;
  printTableHeader("Version");
  printRow("Templated", templatedTiming);
  printRow("Runtime", runtimeTiming);

  double speedup = runtimeTiming.avgMs / templatedTiming.avgMs;
  cout << "\nSpeedup ratio: " << fixed << setprecision(2) << speedup << "x" << endl;
}

// =============================================================================
// Per-EdgeType breakdown (measure each specialization separately)
// =============================================================================

TEST_F(MotionTemplateSpecializationBench, DISABLED_PerEdgeTypeBreakdown) {
  printHeader("Per-EdgeType Performance Breakdown");

  const int NUM_CASES = 500;
  const int ITERATIONS = 100;

  // Generate cases for each edge type
  struct EdgeTypeCase {
    const char* name;
    EdgeType edgeType;
    bool forward;
  };

  vector<EdgeTypeCase> edgeTypes = {
      {"Forward/Word", EdgeType::WordEdge, true},
      {"Forward/Gap", EdgeType::GapEdge, true},
      {"Forward/Next", EdgeType::NextEdge, true},
      {"Back/Word", EdgeType::WordEdge, false},
      {"Back/Gap", EdgeType::GapEdge, false},
      {"Back/Next", EdgeType::NextEdge, false},
  };

  cout << "\n" << setw(14) << left << "EdgeType"
       << "| " << setw(12) << right << "Templated"
       << " | " << setw(12) << "Runtime"
       << " | " << setw(10) << "Speedup" << endl;
  cout << string(14, '-') << "|" << string(14, '-') << "|" << string(14, '-')
       << "|" << string(12, '-') << endl;

  for (const auto& et : edgeTypes) {
    // Generate test cases with fixed direction and edge type
    vector<MotionTestCase> cases;
    for (int i = 0; i < NUM_CASES; i++) {
      int numLines = 10 + (i % 15);
      Lines lines = randomProseLines(numLines, 30, 50);
      int line = RandomGen::range(0, static_cast<int>(lines.size()) - 1);
      int col = lines[line].empty() ? 0 : RandomGen::range(0, static_cast<int>(lines[line].size()) - 1);

      cases.push_back({
          lines,
          Position(line, col),
          et.forward,
          et.edgeType,
          RandomGen::chance(1, 2),  // big
          true                      // skipCurrent
      });
    }

    // Time templated
    TimingStats templated = measureTiming([&]() {
      for (const auto& tc : cases) {
        volatile Position result = VimCore::motionWordCore(
            tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent);
        (void)result;
      }
    }, ITERATIONS);

    // Time runtime
    TimingStats runtime = measureTiming([&]() {
      for (const auto& tc : cases) {
        volatile Position result = Alternate::motionWordCoreRuntime(
            tc.cursor, tc.lines, tc.forward, tc.edgeType, tc.big, tc.skipCurrent);
        (void)result;
      }
    }, ITERATIONS);

    double speedup = runtime.avgMs / templated.avgMs;
    cout << setw(14) << left << et.name
         << "| " << setw(10) << right << fixed << setprecision(3) << templated.avgMs << " ms"
         << " | " << setw(10) << runtime.avgMs << " ms"
         << " | " << setw(8) << setprecision(2) << speedup << "x" << endl;
  }
}
