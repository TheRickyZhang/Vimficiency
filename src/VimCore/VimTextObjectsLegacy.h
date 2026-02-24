#pragma once

#include "Types/Range.h"
#include "Types/Lines.h"

// =============================================================================
// These are still used - don't delete! Arbitrary implementation of inner/outer quote/bracket
// =============================================================================
//
// These functions provide naive implementations for quote and bracket text
// objects. They are kept for Edit.cpp simulation but will be replaced with
// more efficient boundary-based implementations in the future.
//
// For word, paragraph, and sentence text objects, use VimEndpointUtils instead.

namespace VimTextObjectsLegacy {

// Quote text objects (i", a", i', a', i`, a`)
Range innerQuote(const Lines& lines, CursorPos pos, char quote);
Range aroundQuote(const Lines& lines, CursorPos pos, char quote);

// Bracket/paren text objects (i(, a(, i{, a{, i[, a[, i<, a<)
Range innerBracket(const Lines& lines, CursorPos pos, char open, char close);
Range aroundBracket(const Lines& lines, CursorPos pos, char open, char close);

} // namespace VimTextObjectsLegacy
