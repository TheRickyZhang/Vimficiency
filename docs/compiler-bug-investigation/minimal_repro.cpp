// Minimal reproduction of inline function + template argument bug
// Compiler: g++ or clang++ with -std=c++23
//
// Expected: Both TEST 1 and TEST 2 should print the same values
// Actual: TEST 1 (inline) prints garbage, TEST 2 (static) prints correct values
//
// Build: g++ -std=c++23 -O0 minimal_repro.cpp -o minimal_repro && ./minimal_repro

#include <iostream>

struct Params {
  int a = 10;
  int b = 20;
  bool flag = true;
  double c = 3.14;
};

// Inline version - causes bug
inline Params makeParamsInline() {
  Params p{};
  std::cerr << "[makeParamsInline] flag=" << p.flag << " a=" << p.a << std::endl;
  return p;
}

// Static version - works correctly
static Params makeParamsStatic() {
  Params p{};
  std::cerr << "[makeParamsStatic] flag=" << p.flag << " a=" << p.a << std::endl;
  return p;
}

// Template function that receives params
template<typename T>
void processParams(const char* label, T p1, T p2) {
  std::cout << label << ": p1.flag=" << p1.flag << " p1.a=" << p1.a
            << " | p2.flag=" << p2.flag << " p2.a=" << p2.a << std::endl;
}

// Wrapper that calls template with inline functions
void testInline() {
  std::cout << "\n=== TEST 1: inline functions ===" << std::endl;
  processParams("direct call", makeParamsInline(), makeParamsInline());

  std::cout << "\nWith local variables:" << std::endl;
  auto p1 = makeParamsInline();
  auto p2 = makeParamsInline();
  processParams("via locals", p1, p2);
}

// Wrapper that calls template with static functions
void testStatic() {
  std::cout << "\n=== TEST 2: static functions ===" << std::endl;
  processParams("direct call", makeParamsStatic(), makeParamsStatic());

  std::cout << "\nWith local variables:" << std::endl;
  auto p1 = makeParamsStatic();
  auto p2 = makeParamsStatic();
  processParams("via locals", p1, p2);
}

int main() {
  std::cout << "Params default values: a=10, b=20, flag=true, c=3.14" << std::endl;

  testInline();
  testStatic();

  return 0;
}
