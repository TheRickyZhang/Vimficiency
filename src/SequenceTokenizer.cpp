#include "SequenceTokenizer.h"
#include <algorithm>

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
  std::sort(tokens_.begin(), tokens_.end(),
            [](const TokenDef &a, const TokenDef &b) {
              return a.token.size() > b.token.size();
            });
}

bool SequenceTokenizer::tokenize(std::string_view s,
                                 KeySequence &out) const {
  out.clear();
  std::size_t i = 0;

  while (i < s.size()) {
    bool matched = false;

    for (const auto &td : tokens_) {
      const std::string &tok = td.token;
      const std::size_t len  = tok.size();

      if (len <= s.size() - i && s.compare(i, len, tok) == 0) {
        const auto &keys = *td.keys;
        out.insert(out.end(), keys.begin(), keys.end());
        i += len;
        matched = true;
        break;
      }
    }

    if (!matched) {
      // Unknown token starting at position i.
      return false;
    }
  }

  return true;
}
