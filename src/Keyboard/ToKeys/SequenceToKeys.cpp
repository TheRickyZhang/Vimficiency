#include "SequenceToKeys.h"
#include "CharToKeys.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

using namespace std;

SequenceToKeys::SequenceToKeys(const CommandToKeys &actions,
                                     const CommandToKeys &motions) {
  tokens_.reserve(actions.size() + motions.size());

  for (const auto &p : actions) {
    tokens_.push_back(TokenDef{p.first, &p.second});
  }
  for (const auto &p : motions) {
    tokens_.push_back(TokenDef{p.first, &p.second});
  }

  // Longest tokens first, so we greedily match "gg" before "g".
  sort(tokens_.begin(), tokens_.end(),
            [](const TokenDef &a, const TokenDef &b) {
              if (a.token.size() != b.token.size())
                return a.token.size() > b.token.size();
              return a.token < b.token;
            });
}


PhysicalKeys SequenceToKeys::tokenize(string_view s) const {
  PhysicalKeys out;
  size_t i=0;

  while(i < s.size()){
    bool matched=false;

    for(const auto& td: tokens_){
      const string& tok = td.token;
      const size_t len = tok.size();
      if(len<=s.size()-i && s.compare(i,len,tok)==0){
        const auto& keys = *td.keys;
        out.append(keys);
        i += len;
        matched = true;
        break;
      }
    }
    if(!matched){
      // Fallback: single character via CHAR_TO_KEYS (handles typed content)
      char ch = s[i];
      auto it = CHAR_TO_KEYS.find(ch);
      if (it != CHAR_TO_KEYS.end()) {
        out.append(it->second);
        i++;
      } else {
        throw std::runtime_error(
            "Malformed key sequence at byte index " + std::to_string(i));
      }
    }
  }
  return out;
}
