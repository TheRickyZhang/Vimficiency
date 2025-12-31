#include "KeyboardModel.h"
#include <ostream>

KeySequence& KeySequence::append(const KeySequence& ks, size_t cnt){
  if(cnt <= 0 || ks.size() == 0) return *this;
  keys.reserve(keys.size() + ks.size() * cnt);
  for(int i=0; i<cnt; i++) {
    keys.insert(keys.end(), ks.begin(), ks.end());
  }
  return *this;
}

std::ostream& operator<<(std::ostream& os, const KeySequence& ks) {
  for(Key k : ks) {
    os << static_cast<int>(k) << " ";
  }
  return os;
}
