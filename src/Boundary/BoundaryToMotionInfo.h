#pragma once

#include "VimCore/EdgeType.h"

#include <optional>
#include <string_view>

// =============================================================================
// MotionInfo: Parameters for pure motion execution (cursor movement only)
// =============================================================================
//
// Used by VimMovementUtils::motionWord for actual cursor movement.
// Does NOT include deletion-specific logic like cursor exclusivity.
//
// =============================================================================

struct MotionInfo {
    EdgeType edgeType;
    bool isForward;
    bool isWORD;
    bool skipCurrent = false;
};

// =============================================================================
// EditInfo: Parameters for deletion boundary checking
// =============================================================================
//
// IMPORTANT: Deletion and motion edge types differ for some commands!
//
// Motion edges (where cursor lands):      Deletion edges (where delete stops):
//   e  = Forward  + WordEdge                de  = Forward  + WordEdge
//   w  = Forward  + NextEdge                dw  = Forward  + GapEdge  (differs!)
//   b  = Backward + WordEdge                db  = Backward + WordEdge
//   ge = Backward + NextEdge                dge = Backward + NextEdge
//
// This struct includes motion parameters plus deletion-specific fields.
//
// =============================================================================

struct EditInfo {
    EdgeType edgeType;
    bool isForward;
    bool isWORD;
    bool skipCurrent = false;
    // db/dB are the only deletions where cursor char is NOT included.
    // All other deletions (dw, de, dge, etc.) include the cursor char.
    bool isExclusiveAtCursor = false;

    // Extract motion parameters for calling motionWord
    MotionInfo toMotionInfo() const {
        return {edgeType, isForward, isWORD, skipCurrent};
    }
};

// =============================================================================
// Edit Info Constants
// =============================================================================

// word deletions
extern const EditInfo EDIT_DE;   // de: Forward + WordEdge
extern const EditInfo EDIT_DB;   // db: Backward + WordEdge (exclusive at cursor)
extern const EditInfo EDIT_DW;   // dw: Forward + GapEdge
extern const EditInfo EDIT_DGE;  // dge: Backward + NextEdge

// WORD deletions
extern const EditInfo EDIT_DE_BIG;   // dE: Forward + WordEdge
extern const EditInfo EDIT_DB_BIG;   // dB: Backward + WordEdge (exclusive at cursor)
extern const EditInfo EDIT_DW_BIG;   // dW: Forward + GapEdge
extern const EditInfo EDIT_DGE_BIG;  // dgE: Backward + NextEdge

// =============================================================================
// Lookup
// =============================================================================

// Get edit info for a deletion command.
// Returns nullopt for unknown/unsupported commands.
// Supported: de, db, dw, dge, dE, dB, dW, dgE
std::optional<EditInfo> getEditInfo(std::string_view motion);
