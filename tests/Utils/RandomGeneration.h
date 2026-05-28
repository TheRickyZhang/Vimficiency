// tests/Utils/RandomGeneration.h

#pragma once

#include <cstdint>
#include <initializer_list>
#include <random>

template <typename T>
concept Indexable = requires(T t, size_t i) {
  { t[i] };
  { t.size() } -> std::convertible_to<size_t>;
};

class RandomGen {
public:
  static void seed(uint32_t s) { rng().seed(s); }

  static bool chance(int num, int denom) {
    return range(1, denom) <= num;
  }

  // Returns random int in [min, max] inclusive. Portable across stdlib
  // implementations: std::uniform_int_distribution's mapping algorithm is
  // implementation-defined, and libstdc++ vs libc++ produce different
  // outputs from the same mt19937 state. Lemire's nearly-divisionless
  // bounded range gives us identical output everywhere. Residual bias is
  // ~range/2^32, negligible for any test-relevant range.
  static int range(int min, int max) {
    uint32_t span = static_cast<uint32_t>(max - min + 1);
    uint64_t product = static_cast<uint64_t>(rng()()) * span;
    return min + static_cast<int>(product >> 32);
  }

  // Pick random element from any indexable container (equal weight)
  template <Indexable Container>
  static auto pick(const Container& c) {
    return c[range(0, static_cast<int>(c.size()) - 1)];
  }

  // Weighted pool for weighted pick
  template <Indexable Container>
  struct Pool {
    int weight;
    Container pool;
  };

  // Weighted pick from multiple pools
  // Usage: pick<std::string_view>({{3, KEYWORDS}, {1, SYMBOLS}, {1, " "}})
  template <Indexable Container>
  static auto pick(std::initializer_list<Pool<Container>> options) {
    int total = 0;
    for (const auto& opt : options) total += opt.weight;

    int roll = range(1, total);
    int cumulative = 0;
    for (const auto& opt : options) {
      cumulative += opt.weight;
      if (roll <= cumulative) {
        return pick(opt.pool);
      }
    }
    return pick(options.begin()->pool);  // Fallback
  }

  // Direct RNG access for compatibility with existing code
  static std::mt19937& rng() {
    static std::mt19937 instance;
    return instance;
  }
};
