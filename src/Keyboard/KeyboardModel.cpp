#include "KeyboardModel.h"
#include "CharToKeys.h"
#include <ostream>
#include <string>

PhysicalKeys& PhysicalKeys::append(const PhysicalKeys& ks, size_t cnt){
  if(cnt <= 0 || ks.size() == 0) return *this;
  keys.reserve(keys.size() + ks.size() * cnt);
  for(size_t i=0; i<cnt; i++) {
    keys.insert(keys.end(), ks.begin(), ks.end());
  }
  return *this;
}

std::ostream& operator<<(std::ostream& os, const PhysicalKeys& ks) {
  for(Key k : ks) {
    os << static_cast<int>(k) << " ";
  }
  return os;
}

PhysicalKeys makeCountedKeys(const PhysicalKeys& motionKeys, int count) {
  assert(count >= 0);
  if (count == 0) return motionKeys;

  PhysicalKeys keys;
  for (char c : std::to_string(count)) {
    keys.append(CHAR_TO_KEYS.at(c));
  }
  keys.append(motionKeys);
  return keys;
}
