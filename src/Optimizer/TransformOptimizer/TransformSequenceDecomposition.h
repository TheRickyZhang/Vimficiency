#pragma once

#include <string_view>

#include "Types/Token.h"

// Returns the structural prefix token of an edit sequence — the first
// non-typed-text, non-escape token. For `sm<Esc>` returns "s"; for
// `clm<Esc>` returns "cl"; for `x` returns "x". Falls back to the full
// sequence if parsing fails or yields no structural token.
Token extractStructuralToken(std::string_view fullSequence);
