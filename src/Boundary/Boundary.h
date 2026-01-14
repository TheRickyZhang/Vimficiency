#pragma once

#include "BoundaryToMotionInfo.h"
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
// Core API
// =============================================================================

// Check if a motion would extend past the boundary.
//
// Simulates the motion from cursor and checks:
// 1. If motion end position reaches/crosses boundaryPos -> return true
// 2. Otherwise, use crossing function based on char types at edge
//
// Parameters:
// - lines: buffer content
// - cursor: starting cursor position
// - boundaryPos: position just outside the edit region
// - info: motion parameters (direction, endpoint type, isWORD)
//
// Returns true if the motion would extend past the boundary.
bool extendsTooFar(
    const Lines& lines,
    Position cursor,
    Position boundaryPos,
    const MotionInfo& info);

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

