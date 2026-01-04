#pragma once

#include <string>

#include "Editor/Position.h"
#include "Editor/Range.h"
#include "Utils/Lines.h"

// Edit operations that modify buffer content.
// All functions modify Lines and Position in place.

namespace VimEditUtils {

// -----------------------------------------------------------------------------
// Range-based operations (used by operators d, c, y with motions/text objects)
// -----------------------------------------------------------------------------

// Delete text in range, modifies lines and pos
void deleteRange(Lines& lines, const Range& range, Position& pos);

// Replace text in range with new content (for change operations)
// After replacement, cursor is at end of inserted text
void replaceRange(Lines& lines, const Range& range,
                  const std::string& replacement, Position& pos);

// -----------------------------------------------------------------------------
// Single character operations
// -----------------------------------------------------------------------------

// x - delete character under cursor
void deleteChar(Lines& lines, Position& pos);

// X - delete character before cursor
void deleteCharBefore(Lines& lines, Position& pos);

// r<c> - replace character under cursor
void replaceChar(Lines& lines, Position& pos, char newChar);

// ~ - toggle case of character under cursor
void toggleCase(Lines& lines, Position& pos);

// -----------------------------------------------------------------------------
// Line operations
// -----------------------------------------------------------------------------

// dd - delete entire line
void deleteLine(Lines& lines, Position& pos);

// D - delete from cursor to end of line
void deleteToEndOfLine(Lines& lines, Position& pos);

// J - join current line with next (with space)
// gJ - join current line with next (without space)
void joinLines(Lines& lines, Position& pos, bool addSpace);

// o - open new line below
void openLineBelow(Lines& lines, Position& pos);

// O - open new line above
void openLineAbove(Lines& lines, Position& pos);

// cc/S - delete line contents (keep line)
void clearLine(Lines& lines, Position& pos);

// C - delete from cursor to end of line
void changeToEndOfLine(Lines& lines, Position& pos);

// -----------------------------------------------------------------------------
// Insert mode text manipulation
// -----------------------------------------------------------------------------

// Insert text at position, pos updated to end of inserted text
void insertText(Lines& lines, Position& pos, const std::string& text);

// Delete text between start and end positions
void deleteText(Lines& lines, Position start, Position end, Position& pos);

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Get character at position (or '\0' if out of bounds)
char charAt(const Lines& lines, Position pos);

// Check if position is valid
bool isValidPosition(const Lines& lines, Position pos);

// Clamp position to valid range
Position clampPosition(const Lines& lines, Position pos);

} // namespace VimEditUtils
