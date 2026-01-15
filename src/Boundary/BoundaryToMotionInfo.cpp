#include "BoundaryToMotionInfo.h"

// =============================================================================
// word deletions
// =============================================================================
//                             edgeType            fwd    WORD   skip   exclCursor
const EditInfo EDIT_DE  = {EdgeType::WordEdge, true,  false, true,  false};
const EditInfo EDIT_DB  = {EdgeType::WordEdge, false, false, true,  true};   // db excludes cursor
const EditInfo EDIT_DW  = {EdgeType::GapEdge,  true,  false, false, false};
const EditInfo EDIT_DGE = {EdgeType::NextEdge, false, false, false, false};

// =============================================================================
// WORD deletions
// =============================================================================
//                                 edgeType            fwd    WORD  skip   exclCursor
const EditInfo EDIT_DE_BIG  = {EdgeType::WordEdge, true,  true, true,  false};
const EditInfo EDIT_DB_BIG  = {EdgeType::WordEdge, false, true, true,  true};   // dB excludes cursor
const EditInfo EDIT_DW_BIG  = {EdgeType::GapEdge,  true,  true, false, false};
const EditInfo EDIT_DGE_BIG = {EdgeType::NextEdge, false, true, false, false};

// =============================================================================
// Lookup
// =============================================================================

std::optional<EditInfo> getEditInfo(std::string_view motion) {
    // word deletions
    if (motion == "de")  return EDIT_DE;
    if (motion == "db")  return EDIT_DB;
    if (motion == "dw")  return EDIT_DW;
    if (motion == "dge") return EDIT_DGE;
    // WORD deletions
    if (motion == "dE")  return EDIT_DE_BIG;
    if (motion == "dB")  return EDIT_DB_BIG;
    if (motion == "dW")  return EDIT_DW_BIG;
    if (motion == "dgE") return EDIT_DGE_BIG;

    return std::nullopt;
}
