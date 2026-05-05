#pragma once

#include <optional>
#include <string>
#include <string_view>

// Render ambiguous single bytes in Vim bracket notation.
std::string displayChar(char c);

// Inverse for one display-char unit; nullopt for multi-key notation.
std::optional<char> parseDisplayChar(std::string_view s);

// Format a Vim sequence string for human-readable display by tokenizing into
// logical command units and joining with spaces.
std::string formatSequenceForDisplay(std::string_view seq);
