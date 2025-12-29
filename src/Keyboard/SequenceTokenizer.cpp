#include "SequenceTokenizer.h"
#include <algorithm>

using namespace std;

SequenceTokenizer::SequenceTokenizer(const Mapping &actions,
                                     const Mapping &motions) {
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


KeySequence SequenceTokenizer::tokenize(string_view s) const {
  KeySequence out;
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
      // include position + a small preview for debugging
      char ch = s[i];
      throw runtime_error(
        "Malformed key sequence at position " + to_string(i) +
        " near '" + string(1,ch) + "'"
      );
    }
  }
  return out;
}
