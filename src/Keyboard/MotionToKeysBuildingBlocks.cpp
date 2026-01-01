#include "MotionToKeysBuildingBlocks.h"
#include <iostream>
#include <set>

using namespace std;

MotionToKeys combineAll(initializer_list<reference_wrapper<const MotionToKeys>> maps){
  MotionToKeys res;

  for(const auto& mpref : maps){
    const auto& mp = mpref.get();
    for(const auto& [k, v] : mp){
      auto [it, inserted] = res.try_emplace(k, v); // inserts if missing
      if(!inserted){
        if(it->second != v){
          cerr << "combineAll conflict for key: " << k
                    << " old=" << it->second << " new=" << v << "\n";
        }
        it->second = v; // overwrite (keep your current policy)
      }
    }
  }
  return res;
}

CharToKeys combineAllToCharKeySeq( initializer_list<reference_wrapper<const MotionToKeys>> maps)
{
  CharToKeys res;
  for (const auto& mpref : maps) {
    const auto& mp = mpref.get();
    for (const auto& [k, v] : mp) {
      if (k.size() != 1) throw runtime_error("key must be length 1: " + k);
      char c = k[0];
      auto [it, inserted] = res.try_emplace(c, v);
      if (!inserted) {
        if (it->second != v) {
          cerr << "conflict for key '" << c << "'\n";
        }
        it->second = v;
      }
    }
  }
  return res;
}

vector<string>
combineAllMotionsToList(initializer_list<reference_wrapper<const MotionToKeys>> maps){
  set<string> res;
  for(const auto& mpref : maps){
    const auto& mp = mpref.get();
    for(const auto& [k, _] : mp){
      res.insert(k);
    }
  }
  return vector<string>(res.begin(), res.end());
}
