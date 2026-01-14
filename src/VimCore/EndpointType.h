#pragma once

#include <cstdint>

// =============================================================================
// EndpointType: Where a word/WORD motion stops
// =============================================================================
//
// Fundamental building block for word motions. Combined with direction and
// isWORD, this fully specifies a word motion's behavior.
//
// The endpoint determines both:
// - Where the cursor lands after a motion
// - The crossing behavior for deletion commands
//
// =============================================================================

enum class EndpointType : uint8_t {
    End,    // Stop at word boundary (e/ge for motion, de/db for deletion)
    Space,  // Stop after trailing whitespace (w for motion, dw for deletion)
    Next,   // Cross into adjacent word (dge crossing behavior)
    Line    // Stop at line boundary (D, d$, d0)
};
