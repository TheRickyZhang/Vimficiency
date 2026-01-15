#pragma once

#include "VimCore/EdgeType.h"

#include <optional>
#include <string_view>

// =============================================================================
// MotionInfo: Boundary check parameters for deletion operations
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
// This file defines DELETION edge types for boundary crossing checks.
// For pure motion execution, use VimMovementUtils::motionWord directly.
//
// =============================================================================

struct MotionInfo {
    EdgeType edgeType;
    bool isForward;
    bool isWORD;
    bool skipCurrent = false;
};

// =============================================================================
// Deletion Info Constants
// =============================================================================

// word deletions
extern const MotionInfo MOTION_DE;   // de: Forward + WordEdge
extern const MotionInfo MOTION_DB;   // db: Backward + WordEdge
extern const MotionInfo MOTION_DW;   // dw: Forward + GapEdge
extern const MotionInfo MOTION_DGE;  // dge: Backward + NextEdge

// WORD deletions
extern const MotionInfo MOTION_DE_BIG;   // dE: Forward + WordEdge
extern const MotionInfo MOTION_DB_BIG;   // dB: Backward + WordEdge
extern const MotionInfo MOTION_DW_BIG;   // dW: Forward + GapEdge
extern const MotionInfo MOTION_DGE_BIG;  // dgE: Backward + NextEdge

// =============================================================================
// Lookup
// =============================================================================

// Get motion info for a deletion command.
// Returns nullopt for unknown/unsupported motions.
// Supported: de, db, dw, dge, dE, dB, dW, dgE
std::optional<MotionInfo> getMotionInfo(std::string_view motion);
