#include <gtest/gtest.h>
#include "Boundary/BoundaryToMotionInfo.h"
#include "VimCore/VimMovementUtils.h"
#include "Editor/Position.h"
#include "Utils/Debug.h"

// Debug test for specific failing case:
// Input: "Q,, \t"   (Q=0, ,=1, ,=2, space=3, tab=4)
// de from col 2, boundary at col 4
// Expected: CROSSED (deletion reaches end of buffer including tab)

TEST(BoundaryDebugTest, DeForwardWhitespaceAtEnd) {
    Lines lines = {"Q,, \t"};  // 5 chars: Q, comma, comma, space, tab
    Position cursor(0, 2);      // cursor at second comma
    Position boundary(0, 4);    // boundary at tab

    // de uses WordEdge, forward, skipCurrent=true
    EditInfo info = EDIT_DE;

    std::cerr << "\n=== Testing de from col 2, boundary at col 4 ===" << std::endl;
    std::cerr << "Input: \"Q,, \\t\" (5 chars)" << std::endl;
    std::cerr << "Cursor: col 2 (',')" << std::endl;
    std::cerr << "Boundary: col 4 ('\\t')" << std::endl;

    bool result = VimMovementUtils::checkMotionWordReaches(
        cursor, boundary, lines,
        info.isForward, info.edgeType, info.isWORD, info.skipCurrent);

    std::cerr << consume_debug_output() << std::endl;
    std::cerr << "checkMotionWordReaches returned: " << (result ? "true (EXTENDS)" : "false (SAFE)") << std::endl;

    // The motion should reach the boundary because:
    // - skipCurrent: step from col 2 to col 3 (space)
    // - motionWordCore from space: try to find word end
    // - step to col 4 (tab, still blank)
    // - step forward from col 4 -> can't (end of buffer)
    // - Motion ends at buffer end, which is >= boundary
    EXPECT_TRUE(result) << "de from whitespace should reach boundary at end of buffer";
}
