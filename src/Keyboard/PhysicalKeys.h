#pragma once

#include <cassert>
#include <iosfwd>
#include <span>
#include <utility>
#include <vector>

#include "Key.h"

// Represents physical key presses for effort calculation.
// Used by RunningEffort to compute typing cost based on hand/finger patterns.
class PhysicalKeys {
  std::vector<Key> keys;

public:
  PhysicalKeys() = default;
  PhysicalKeys(std::initializer_list<Key> init) : keys(init) {}
  PhysicalKeys(std::vector<Key> init) : keys(std::move(init)) {}
  PhysicalKeys(Key k, size_t count) : keys(count, k) {}
  PhysicalKeys(std::span<const Key> s) : keys(s.begin(), s.end()) {}
  PhysicalKeys(std::initializer_list<std::span<const Key>> parts) {
    size_t total = 0;
    for (std::span<const Key> part : parts) {
      total += part.size();
    }
    keys.reserve(total);
    for (std::span<const Key> part : parts) {
      keys.insert(keys.end(), part.begin(), part.end());
    }
  }

  size_t size() const { return keys.size(); }
  bool empty() const { return keys.empty(); }
  auto begin() const { return keys.begin(); }
  auto end() const { return keys.end(); }
  void reserve(int n) { keys.reserve(n); }

  std::span<const Key> view() const { return keys; }
  void push_back(const Key& k) { keys.push_back(k); }

  PhysicalKeys& append(const PhysicalKeys& ks, size_t cnt = 1);

  PhysicalKeys asChange() const {
    assert(keys[0] == Key::Key_D);
    PhysicalKeys result = *this;
    result.keys[0] = Key::Key_C;
    return result;
  }

  PhysicalKeys& operator+=(const PhysicalKeys& other) { return append(other); }
  bool operator==(const PhysicalKeys& other) const {
    return keys == other.keys;
  }
  bool operator!=(const PhysicalKeys& other) const { return !(*this == other); }
};

std::ostream& operator<<(std::ostream& os, const PhysicalKeys& ks);
