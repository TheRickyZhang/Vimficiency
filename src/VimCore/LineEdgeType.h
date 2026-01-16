#pragma once

#include <cstdint>

// =============================================================================
// LineEdgeType: Where a paragraph/linewise motion stops
// =============================================================================
//
// Parallel to EdgeType but for linewise operations (paragraphs).
// Combined with direction, this fully specifies a paragraph motion's behavior.
//
// These edge types are DIRECTION-INDEPENDENT. The physical line depends
// on the direction of travel, but the concept is the same:
//
//   BlockEdge: Edge of current same-type block (blank or non-blank lines)
//   GapEdge:   Edge of blank line run (adjacent to current paragraph)
//   NextEdge:  Start/end of next different-type block
//
// Mapping to word EdgeType:
//   BlockEdge ↔ WordEdge (edge of current unit)
//   GapEdge   ↔ GapEdge  (edge of gap)
//   NextEdge  ↔ NextEdge (start/end of next unit)
//
// =============================================================================

enum class LineEdgeType : uint8_t {
    BlockEdge,  // Edge of current block (ip text object)
    GapEdge,    // Edge of blank line run (ap with trailing blanks)
    NextEdge    // Edge of next block (}, { motions)
};
