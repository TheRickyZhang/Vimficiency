#pragma once

#include "EditBoundary.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"

// =============================================================================
// Boundary Crossing API
// =============================================================================
//
// Combines motion simulation with EditBoundary crossing logic to determine
// if a deletion motion would extend past a boundary.
//
// Flow:
// 1. Simulate motion to get end position
// 2. If end position reaches/crosses boundary position -> return true
// 3. Otherwise, use crossing function based on char types
//
// =============================================================================

// =============================================================================
// Helper: Get char type at position
// =============================================================================

// Get CharType at a specific position in the buffer.
// Returns Newline if position is at line boundary or out of bounds.
CharType getCharTypeAt(const Lines& lines, Position pos);

// Get CharType of char before position (for backward motion edge checks).
// Skips newlines (crosses to previous line if needed).
CharType getCharTypeBefore(const Lines& lines, Position pos);

// Get CharType of char after position (for forward motion edge checks).
// Skips newlines (crosses to next line if needed).
CharType getCharTypeAfter(const Lines& lines, Position pos);

