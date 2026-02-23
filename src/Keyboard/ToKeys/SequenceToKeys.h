#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "CommandToKeys.h"
#include "Keyboard/PhysicalKeys.h"

// Used for physical key presses (calculating effort) only!
// For semantic tokenization, see parseMotions() in Motion.h

class SequenceToKeys {
public:
  // std::less<> enables transparent comparison (lookup with string_view without allocation)

  // Build from action + motion maps (they must outlive the tokenizer).
  SequenceToKeys(const CommandToKeys &actions,
                    const CommandToKeys &motions);

  PhysicalKeys tokenize(std::string_view s) const;

private:
  struct TokenDef {
    std::string      token;
    const PhysicalKeys *keys; // non-owning, points into mappings
  };

  std::vector<TokenDef> tokens_; // sorted by descending token length
};
