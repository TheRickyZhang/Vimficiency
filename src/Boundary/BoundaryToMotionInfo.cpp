#include "BoundaryToMotionInfo.h"

// =============================================================================
// word motions
// =============================================================================

const MotionInfo MOTION_DE  = {EndpointType::End,   true,  false};
const MotionInfo MOTION_DB  = {EndpointType::End,   false, false};
const MotionInfo MOTION_DW  = {EndpointType::Space, true,  false};
const MotionInfo MOTION_DGE = {EndpointType::Next,  false, false};

// =============================================================================
// WORD motions
// =============================================================================

const MotionInfo MOTION_DE_BIG  = {EndpointType::End,   true,  true};
const MotionInfo MOTION_DB_BIG  = {EndpointType::End,   false, true};
const MotionInfo MOTION_DW_BIG  = {EndpointType::Space, true,  true};
const MotionInfo MOTION_DGE_BIG = {EndpointType::Next,  false, true};

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
