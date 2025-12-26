#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "KeyboardModel.h" // for Key

class SequenceTokenizer {
public:
  using KeySequence = std::vector<Key>;
  using Mapping     = std::map<std::string, KeySequence>;

  // Build from action + motion maps (they must outlive the tokenizer).
  SequenceTokenizer(const Mapping &actions,
                    const Mapping &motions);

  // Returns true on success, false if `s` contains an unknown token.
  // On success, `out` will contain the flattened key sequence.
  bool tokenize(std::string_view s, KeySequence &out) const;

private:
  struct TokenDef {
    std::string      token;
    const KeySequence *keys; // non-owning, points into mappings
  };

  std::vector<TokenDef> tokens_; // sorted by descending token length
};
