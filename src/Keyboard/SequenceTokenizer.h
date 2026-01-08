#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "KeyboardModel.h"
#include "StringToKeys.h"

// Used for physical key presses (calculating effort) only!
// For semantic tokenization, see parseMotions() in Motion.h

class SequenceTokenizer {
public:
  // std::less<> enables transparent comparison (lookup with string_view without allocation)

  // Build from action + motion maps (they must outlive the tokenizer).
  SequenceTokenizer(const StringToKeys &actions,
                    const StringToKeys &motions);

  PhysicalKeys tokenize(std::string_view s) const;

private:
  struct TokenDef {
    std::string      token;
    const PhysicalKeys *keys; // non-owning, points into mappings
  };

  std::vector<TokenDef> tokens_; // sorted by descending token length
};
