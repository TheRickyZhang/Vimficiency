#include "CountToKeys.h"

#include "Keyboard/PhysicalKeys.h"

#include <array>
#include <cassert>
#include <string>
#include <string_view>

namespace CountToKeys {
namespace {
constexpr std::array<Key, 10> DIGIT_KEYS = {
    Key::Key_0, Key::Key_1, Key::Key_2, Key::Key_3, Key::Key_4,
    Key::Key_5, Key::Key_6, Key::Key_7, Key::Key_8, Key::Key_9,
};

const std::array<PhysicalKeys, MAX_PREFIX_COUNT + 1> PREFIX_KEYS = [] {
  std::array<PhysicalKeys, MAX_PREFIX_COUNT + 1> table;
  for (int count = 1; count <= MAX_PREFIX_COUNT; ++count) {
    if (count >= 10) {
      table[count].push_back(DIGIT_KEYS[count / 10]);
    }
    table[count].push_back(DIGIT_KEYS[count % 10]);
  }
  return table;
}();

const std::array<std::string, MAX_PREFIX_COUNT + 1> PREFIX_TEXT_STORAGE = [] {
  std::array<std::string, MAX_PREFIX_COUNT + 1> table;
  table[0] = "";
  for (int count = 1; count <= MAX_PREFIX_COUNT; ++count) {
    table[count] = std::to_string(count);
  }
  return table;
}();

const std::array<std::string_view, MAX_PREFIX_COUNT + 1> PREFIX_TEXT = [] {
  std::array<std::string_view, MAX_PREFIX_COUNT + 1> table;
  for (int count = 0; count <= MAX_PREFIX_COUNT; ++count) {
    table[count] = PREFIX_TEXT_STORAGE[count];
  }
  return table;
}();
}  // namespace

const PhysicalKeys& keysForCount(int count) {
  assert(count >= 0 && count <= MAX_PREFIX_COUNT);
  return PREFIX_KEYS[count];
}

std::string_view textForCount(int count) {
  assert(count >= 0 && count <= MAX_PREFIX_COUNT);
  return PREFIX_TEXT[count];
}

}  // namespace CountToKeys
