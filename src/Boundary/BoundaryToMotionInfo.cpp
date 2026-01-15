#include "BoundaryToMotionInfo.h"

// =============================================================================
// word deletions
// =============================================================================

const MotionInfo MOTION_DE  = {EdgeType::WordEdge, true,  false, true};
const MotionInfo MOTION_DB  = {EdgeType::WordEdge, false, false, true};
const MotionInfo MOTION_DW  = {EdgeType::GapEdge,  true,  false};
const MotionInfo MOTION_DGE = {EdgeType::NextEdge, false, false};

// =============================================================================
// WORD deletions
// =============================================================================

const MotionInfo MOTION_DE_BIG  = {EdgeType::WordEdge, true,  true, true};
const MotionInfo MOTION_DB_BIG  = {EdgeType::WordEdge, false, true, true};
const MotionInfo MOTION_DW_BIG  = {EdgeType::GapEdge,  true,  true};
const MotionInfo MOTION_DGE_BIG = {EdgeType::NextEdge, false, true};

// =============================================================================
// Lookup
// =============================================================================

std::optional<MotionInfo> getMotionInfo(std::string_view motion) {
    // word motions
    if (motion == "de")  return MOTION_DE;
    if (motion == "db")  return MOTION_DB;
    if (motion == "dw")  return MOTION_DW;
    if (motion == "dge") return MOTION_DGE;
    // WORD motions
    if (motion == "dE")  return MOTION_DE_BIG;
    if (motion == "dB")  return MOTION_DB_BIG;
    if (motion == "dW")  return MOTION_DW_BIG;
    if (motion == "dgE") return MOTION_DGE_BIG;

    return std::nullopt;
}
