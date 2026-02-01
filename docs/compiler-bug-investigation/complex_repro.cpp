// More complex reproduction attempting to match the actual bug scenario
// Key elements:
// 1. Struct with default member initializers
// 2. Inline functions at file scope
// 3. Static member functions in a class calling those inline functions
// 4. Template function with constexpr bool parameter
// 5. Loop calling the template multiple times with different data

#include <iostream>
#include <vector>
#include <string>

struct Params {
  int maxResults = 10;
  int maxNodes = 50000;
  double factor = 2.0;
  int threshold = 3;
  double weight1 = 1.0;
  double weight2 = 1.0;
  bool useFeature = true;  // This is the problematic field
  bool debug = false;

  Params& withFeature(bool v) { useFeature = v; return *this; }
  Params& withMaxNodes(int v) { maxNodes = v; return *this; }
};

constexpr bool ENABLE_COMPARISON = true;

// Inline helpers at file scope
inline Params paramsA() {
  return Params{};
}

inline Params paramsB() {
  return Params{}.withFeature(false);
}

// Template that processes params
template<bool EnableComparison, typename SetupT, typename ParamsT>
void runBenchmark(const std::string& label, const SetupT& setup, ParamsT pA, ParamsT pB) {
  if constexpr (EnableComparison) {
    std::cout << label << ": pA.useFeature=" << pA.useFeature
              << " pB.useFeature=" << pB.useFeature
              << " setup.lines=" << setup.numLines << std::endl;
  }
}

struct BenchSetup {
  int numLines;
  std::vector<std::string> data;

  BenchSetup(int n) : numLines(n) {
    for (int i = 0; i < n; i++) {
      data.push_back("line " + std::to_string(i));
    }
  }
};

class Benchmark {
public:
  static void printRow(const std::string& label, const BenchSetup& setup) {
    runBenchmark<ENABLE_COMPARISON>(label, setup, paramsA(), paramsB());
  }

  static void runTest() {
    std::cout << "=== Running benchmark with inline paramsA/paramsB ===" << std::endl;
    for (int numLines : {1, 5, 10, 15, 20}) {
      BenchSetup setup(numLines);
      printRow(std::to_string(numLines), setup);
    }
  }
};

int main() {
  std::cout << "Expected: pA.useFeature=1, pB.useFeature=0 for all rows\n" << std::endl;
  Benchmark::runTest();
  return 0;
}
