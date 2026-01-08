#include "EditToKeysPrimitives.h"
#include "CharToKeys.h"

#include <iostream>
#include <stdexcept>

using namespace std;

// combineAll is provided by MotionToKeysPrimitives.cpp (same underlying type)

// Convert a string to PhysicalKeys using CHAR_TO_KEYS
static PhysicalKeys stringToKeys(const string& s) {
  PhysicalKeys result;
  for (char c : s) {
    auto it = CHAR_TO_KEYS.find(c);
    if (it == CHAR_TO_KEYS.end()) {
      throw runtime_error("makeCombinations: unknown char '" + string(1, c) + "'");
    }
    for (Key k : it->second) {
      result.push_back(k);
    }
  }
  return result;
}

EditToKeys makeCombinations(initializer_list<vector<string>> slots) {
  EditToKeys result;

  // Convert initializer_list to vector for indexing
  vector<vector<string>> slotVec(slots);
  if (slotVec.empty()) return result;

  // Generate all combinations using indices
  vector<size_t> indices(slotVec.size(), 0);

  while (true) {
    // Build current combination
    string combo;
    for (size_t i = 0; i < slotVec.size(); i++) {
      combo += slotVec[i][indices[i]];
    }
    result[combo] = stringToKeys(combo);

    // Increment indices (rightmost first, like counting)
    size_t pos = slotVec.size();
    while (pos > 0) {
      pos--;
      indices[pos]++;
      if (indices[pos] < slotVec[pos].size()) break;
      indices[pos] = 0;
      if (pos == 0) return result;  // All combinations done
    }
  }
}
