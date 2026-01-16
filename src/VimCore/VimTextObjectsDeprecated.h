#pragma once

#include <string>
#include <vector>

#include "Editor/Range.h"

// =============================================================================
// DEPRECATED: Quote and Bracket text object implementations
// =============================================================================
//
// These functions provide naive implementations for quote and bracket text
// objects. They are kept for Edit.cpp simulation but will be replaced with
// more efficient boundary-based implementations in the future.
//
// For word, paragraph, and sentence text objects, use VimEndpointUtils instead.

namespace VimTextObjectsDeprecated {

// Quote text objects (i", a", i', a', i`, a`)
Range innerQuote(const std::vector<std::string>& lines, Position pos, char quote);
Range aroundQuote(const std::vector<std::string>& lines, Position pos, char quote);

// Bracket/paren text objects (i(, a(, i{, a{, i[, a[, i<, a<)
Range innerBracket(const std::vector<std::string>& lines, Position pos, char open, char close);
Range aroundBracket(const std::vector<std::string>& lines, Position pos, char open, char close);

} // namespace VimTextObjectsDeprecated
