#pragma once

#include <cstdint>

// =============================================================================
// SentenceEdgeType: Where a sentence motion stops
// =============================================================================
//
// Parallel to EdgeType (words) and LineEdgeType (paragraphs).
// Combined with direction, this fully specifies a sentence motion's behavior.
//
// These edge types are DIRECTION-INDEPENDENT. The physical position depends
// on the direction of travel, but the concept is the same:
//
//   SentenceEdge: Edge of current sentence (at punctuation mark)
//   GapEdge:      Edge of whitespace gap after sentence end
//   NextEdge:     Start of next sentence ()/( motions)
//
// Mapping to word EdgeType:
//   SentenceEdge ↔ WordEdge (edge of current unit)
//   GapEdge      ↔ GapEdge  (edge of gap)
//   NextEdge     ↔ NextEdge (start/end of next unit)
//
// Sentence boundaries are defined by:
//   1. Punctuation [.!?] followed by optional closers [)'"'\]] and whitespace/EOL
//   2. Blank lines (paragraph boundary = sentence boundary)
//
// =============================================================================

enum class SentenceEdgeType : uint8_t {
    SentenceEdge,  // Edge of sentence (punctuation + closers)
    GapEdge,       // Edge of whitespace gap after sentence end
    NextEdge       // Start of next sentence ()/( motions)
};
