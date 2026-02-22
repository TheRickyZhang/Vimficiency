// tests/Utils/EditTestGenerators.h
//
// Edit-specific test utilities for EditOptimizer tests.
// Builds on RandomBufferHelpers for core random generation.
// Provides embedded edit region creation and flat position indexing.

#pragma once

#include "Boundary/EditBoundary.h"
#include "VimTypes/Position.h"
#include "VimTypes/Lines.h"
#include "Utils/RandomBufferHelpers.h"  // Core random generation

#include <string>

// =============================================================================
// Position Index Utilities
// =============================================================================

// Convert (row, col) to flat index for typeAllResults access.
// Note: This matches how EditOptimizer indexes results - by position count,
// NOT including newlines. Empty lines count as 1 position.
int toFlatIndex(int row, int col, const Lines& lines);

// Convert flat index back to (row, col) position
Position fromFlatIndex(int flatIdx, const Lines& lines);

// =============================================================================
// Embedded Edit Region
// =============================================================================

// Represents an edit region embedded within a larger buffer.
// Used to test that EditOptimizer sequences preserve content outside the edit region.
//
// Example:
//   fullBuffer = ["prefix_content0", "content1", "content2_suffix"]
//   editRegion = ["content0", "content1", "content2"]
//   prefix = "prefix_"
//   suffix = "_suffix"
//
struct EmbeddedEditRegion {
  Lines fullBuffer;      // Complete buffer with prefix/suffix
  Lines editRegion;      // Just the edit content (what optimizer sees)
  std::string prefix;    // Content before edit region on first line
  std::string suffix;    // Content after edit region on last line

  // Boundary info
  int startLine;         // Edit region start in fullBuffer coordinates
  int startCol;          // Edit region start column
  int endLine;           // Edit region end in fullBuffer coordinates
  int endCol;            // Edit region end column (exclusive)

  // Construct EditBoundary for use with EditOptimizer
  EditBoundary makeBoundary() const;

  // Convert edit region position to fullBuffer position
  Position toFullBufferPos(const Position& editPos) const;

  // Expected result after deleting edit region: prefix + suffix
  std::string expectedAfterDeletion() const;
};

// =============================================================================
// EmbeddedEditRegion Generators
// =============================================================================

// Generate a single-line embedded edit region:
//   fullBuffer = ["prefix_editContent_suffix"]
EmbeddedEditRegion generateSingleLineEmbedded(
    int prefixLen,    // 1-3 typical
    int contentLen,   // 3-8 typical
    int suffixLen     // 1-3 typical
);

// Generate a multi-line embedded edit region:
//   fullBuffer[0] = prefix + editRegion[0]
//   fullBuffer[1..n-2] = editRegion[1..n-2]
//   fullBuffer[n-1] = editRegion[n-1] + suffix
EmbeddedEditRegion generateMultiLineEmbedded(
    int numLines,     // 2-5 typical
    int minLineLen,   // 3 typical
    int maxLineLen,   // 8 typical
    int prefixLen,    // 1-6 typical
    int suffixLen     // 1-6 typical
);

// Generate with random parameters within typical ranges
EmbeddedEditRegion generateRandomSingleLineEmbedded();
EmbeddedEditRegion generateRandomMultiLineEmbedded();
