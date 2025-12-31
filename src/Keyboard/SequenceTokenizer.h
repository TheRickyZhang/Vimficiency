#pragma once

#include <functional>  // for std::less<>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "KeyboardModel.h"

// Used for physical key presses (calculating effort) only!
// For semantic tokenization, see parseMotions() in Motion.h

class SequenceTokenizer {
public:
  // std::less<> enables transparent comparison (lookup with string_view without allocation)
  using Mapping     = std::map<std::string, KeySequence, std::less<>>;

  // Build from action + motion maps (they must outlive the tokenizer).
  SequenceTokenizer(const Mapping &actions,
                    const Mapping &motions);

  KeySequence tokenize(std::string_view s) const;

private:
  struct TokenDef {
    std::string      token;
    const KeySequence *keys; // non-owning, points into mappings
  };

  std::vector<TokenDef> tokens_; // sorted by descending token length
};
