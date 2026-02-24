#pragma once

#include <cstdint>

// =============================================================================
// EdgeType: Where a word/WORD motion stops
// =============================================================================
//
// Fundamental building block for word motions. Combined with direction and
// isWORD, this fully specifies a word motion's behavior.
//
// These edge types are DIRECTION-INDEPENDENT. The physical position depends
// on the direction of travel, but the concept is the same:
//
//   WordEdge: The edge of the word we traversed (step back into the word)
//   GapEdge:  The edge of the gap before the next word (step back into gap)
//   NextEdge: The edge of the next unit (stay at first char of next thing)
//   LineEdge: The edge of the line (for line-based operations)
//
// =============================================================================

enum class EdgeType : uint8_t {
    WordEdge,  // Edge of the word (e/ge motion, de/db deletion)
    GapEdge,   // Edge of the gap before next word (dw deletion)
    NextEdge,  // Edge of the next unit (w/b motion, dge deletion)
    LineEdge   // Edge of the line (D, d$, d0)
};
