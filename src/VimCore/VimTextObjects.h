#pragma once

#include <string>
#include <vector>

#include "Editor/Range.h"

// Text objects return a Range representing the selected region.
// "Inner" objects exclude surrounding delimiters/whitespace.
// "Around" objects include surrounding delimiters/whitespace.
//
// These are used with operators: ciw, daw, yi", da(, etc.

namespace VimTextObjects {

// Word text objects (iw, aw, iW, aW)
// bigWord: true for W/WORD, false for w/word
Range innerWord(const std::vector<std::string>& lines, Position pos, bool bigWord = false);
Range aroundWord(const std::vector<std::string>& lines, Position pos, bool bigWord = false);

// Paragraph text objects (ip, ap)
Range innerParagraph(const std::vector<std::string>& lines, Position pos);
Range aroundParagraph(const std::vector<std::string>& lines, Position pos);

// Quote text objects (i", a", i', a', i`, a`)
Range innerQuote(const std::vector<std::string>& lines, Position pos, char quote);
Range aroundQuote(const std::vector<std::string>& lines, Position pos, char quote);

// Bracket/paren text objects (i(, a(, i{, a{, i[, a[, i<, a<)
Range innerBracket(const std::vector<std::string>& lines, Position pos, char open, char close);
Range aroundBracket(const std::vector<std::string>& lines, Position pos, char open, char close);

// Sentence text objects (is, as) - TODO if needed
// Range innerSentence(const std::vector<std::string>& lines, Position pos);
// Range aroundSentence(const std::vector<std::string>& lines, Position pos);

} // namespace VimTextObjects
