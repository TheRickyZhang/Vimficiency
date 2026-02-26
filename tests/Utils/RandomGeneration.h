// tests/Utils/RandomGeneration.h

#pragma once

#include <initializer_list>
#include <map>
#include <random>
#include <utility>

#include "Utils/SeedManager.h"

template <typename T>
concept Indexable = requires(T t, size_t i) {
  { t[i] };
  { t.size() } -> std::convertible_to<size_t>;
};

class RandomGen {
public:
  static void seed(uint32_t s) { rng().seed(s); }

  static bool chance(int num, int denom) {
    return dist(1, denom)(rng()) <= num;
  }

  // Returns random int in [min, max] inclusive
  static int range(int min, int max) { return dist(min, max)(rng()); }

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

private:
  using DistKey = std::pair<int, int>;
  using DistMap = std::map<DistKey, std::uniform_int_distribution<int>>;

  static DistMap& cache() {
    static DistMap instance;
    return instance;
  }

  static std::uniform_int_distribution<int>& dist(int min, int max) {
    DistKey key{min, max};
    auto it = cache().find(key);
    if (it == cache().end()) {
      it = cache()
               .emplace(key, std::uniform_int_distribution<int>(min, max))
               .first;
    }
    return it->second;
  }
};
