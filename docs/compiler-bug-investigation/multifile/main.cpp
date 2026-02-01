// Test file - simulates benchmark file
#include "params.h"
#include "processor.h"
#include <vector>
#include <string>

// External library function
extern void processWithParams(const Params& p);

constexpr bool ENABLE_COMPARISON = true;

// Inline helpers - THE SUSPECT
inline Params paramsA() {
  return Params{};
}

inline Params paramsB() {
  return Params{}.withFeature(false);
}

struct BenchSetup {
  int numLines;
  std::vector<std::string> data;
  BenchSetup(int n) : numLines(n) {
    for (int i = 0; i < n; i++) data.push_back("line");
  }
};

struct BenchResult {
  int count;
};

class Benchmark {
public:
  static BenchResult runWithParams(const BenchSetup& setup, const Params& p) {
    processWithParams(p);
    return {setup.numLines};
  }

  static void printRow(const std::string& label, const BenchSetup& setup) {
    runUnifiedBenchmark<ENABLE_COMPARISON>(
        label, setup, paramsA(), paramsB(),
        [](const BenchSetup& s, const Params& p) {
          return runWithParams(s, p);
        });
  }

  static void run() {
    std::cout << "Expected: A.useFeature=1, B.useFeature=0\n" << std::endl;
    for (int n : {1, 5, 10, 15, 20}) {
      BenchSetup setup(n);
      printRow(std::to_string(n), setup);
    }
  }
};

int main() {
  Benchmark::run();
  return 0;
}
