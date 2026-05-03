#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Half-open byte range identifying the sub-sequence that performs one
// planned composition edit. Navigation rows are derived as the gaps between
// consecutive spans (and before the first / after the last span) over the
// flat key sequence.
//
// Used by Explore display only; the universal `optimize()` path does not
// produce these. Produced exactly once per planned edit by the traced
// composition search (see `EditSpanTrace` in CompositionOptimizer.cpp).
struct EditSequenceSpan {
  uint32_t beginByte = 0;
  uint32_t endByte = 0;
  bool operator==(const EditSequenceSpan&) const = default;
};

// Build display rows for the Explore "Explored" / "Optimal N" header columns
// from a flat key sequence and its plan-aligned edit spans.
//
// Rules:
// - Non-empty bytes between spans (and before/after the first/last span)
//   become navigation rows.
// - Each non-empty span becomes one edit row.
// - Empty chunks (zero-length gap or zero-length span) are skipped.
// - When `spans` is empty and `seq` is non-empty, returns one row containing
//   the whole sequence (pure-motion case).
std::vector<std::string> rowsFromEditSpans(
    std::string_view seq,
    std::span<const EditSequenceSpan> spans);
