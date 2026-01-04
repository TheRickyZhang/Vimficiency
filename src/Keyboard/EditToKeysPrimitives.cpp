#include "EditToKeysPrimitives.h"

#include <iostream>

using namespace std;

EditToKeys combineAll(initializer_list<reference_wrapper<const EditToKeys>> maps) {
  EditToKeys res;

  for (const auto& mpref : maps) {
    const auto& mp = mpref.get();
    for (const auto& [k, v] : mp) {
      auto [it, inserted] = res.try_emplace(k, v);
      if (!inserted) {
        if (it->second != v) {
          cerr << "combineAll conflict for key: " << k
               << " old=" << it->second << " new=" << v << "\n";
        }
        it->second = v;
      }
    }
  }
  return res;
}
