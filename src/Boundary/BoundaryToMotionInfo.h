#pragma once

#include "VimCore/EndpointType.h"

#include <optional>
#include <string_view>

// =============================================================================
// MotionInfo: Boundary check parameters for deletion operations
// =============================================================================
//
// IMPORTANT: Deletion and motion endpoint types differ for some commands!
//
// Motion endpoints (where cursor lands):    Deletion endpoints (where delete stops):
//   e  = Forward + End                        de  = Forward + End
//   w  = Forward + Next                       dw  = Forward + Space  (differs!)
//   b  = Backward + Next                      db  = Backward + End   (differs!)
//   ge = Backward + End                       dge = Backward + Next  (differs!)
//
// This file defines DELETION endpoint types for boundary crossing checks.
// For pure motion execution, use VimMovementUtils::motionWord directly.
//
// =============================================================================

struct MotionInfo {
    EndpointType endpointType;
    bool isForward;
    bool isWORD;
    bool skipCurrent = false;
};

// =============================================================================
// Deletion Info Constants
// =============================================================================

// word deletions
extern const MotionInfo MOTION_DE;   // de: Forward + End
extern const MotionInfo MOTION_DB;   // db: Backward + End
extern const MotionInfo MOTION_DW;   // dw: Forward + Space
extern const MotionInfo MOTION_DGE;  // dge: Backward + Next

// WORD deletions
extern const MotionInfo MOTION_DE_BIG;   // dE: Forward + End
extern const MotionInfo MOTION_DB_BIG;   // dB: Backward + End
extern const MotionInfo MOTION_DW_BIG;   // dW: Forward + Space
extern const MotionInfo MOTION_DGE_BIG;  // dgE: Backward + Next

// =============================================================================
// Lookup
// =============================================================================

// Get motion info for a deletion command.
// Returns nullopt for unknown/unsupported motions.
// Supported: de, db, dw, dge, dE, dB, dW, dgE
std::optional<MotionInfo> getMotionInfo(std::string_view motion);
