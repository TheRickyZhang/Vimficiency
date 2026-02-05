# CompositionOptimizer
- Finds best way to perform changes to a buffer by breaking it down into a series of edit regions to be completed in order.
- Heuristic: Estimated suffix cost of edits not completed + distance to next edit (+ effort)

## Diff Generation
- Our first step is to perform a character-level Myers diff analysis (similar algorithm to git). We separate into individual diffs by some heuristics, which currently are:
  - Match count >= MIN_MATCH_LENGTH -> separate, but otherwise merge
  - Don't count matches across new lines as much (likely share much tab whitespace)
  - Don't include boundary at end, cut off exactly since no more content
  - Have exceptions for well-formed short content inside "", (), etc. (To be expanded upon)
- Using that, we track intermediate buffer states, compute suffix cost sums, and calculate a diff for each edit region

## Composition Logic
- By abstracting away individual edits to movements over a one-step diff change, our search alternates between call types:
  - Intra-edit: refer to EditResult to apply an edit
  - Inter-edit: move from edit end to a next edit start (MotionOptimizer::optimizeToRange)
- By storing the previous edit command, we can also quickly check if . will work. (TODO)

## Quote/Bracket Motions
- Motions like ci"/cab allow us to combine an Inter-edit with the next Intra-edit.
- Since we are moving in order, quotes are only valid if the first quote on this line, and the quote after that are within the next edit region.
(Note "within" could be one outside region, as doing iw -> still only affect what is inside. Greedily picking inside if matches is optimal)
- Brackets are a bit more complex because they must form a MATCHING pair, but we can simply track bracket depth with a stack (balance of 0 = will search right), and we can leverage the fact that actions will match with the outermost bracket in region
- Thus, we can use a bitmask prefix for quotes, matching with FIRST pair in region, and count prefix for brackets, matching with OUTERMOST pair in region.
- Since each edit can introduce new destructive content, we must calculate for each edit.

## Pure Insertions
- Because a pure insertion has no starting point, we must handle it from the higher composition level.
- We perform a similar movement search, but augmented with the option of using o/O if we need a new line, and I/A in place of a final $/^ movement, and i/a otherwise.
- Each strategy defines a range of valid cursor positions from which its mode-entry command produces the correct edit:
  - `o`: any column on the line above (for new-line insertions at col 0)
  - `I`: any column on the target line (inserts at first non-blank)
  - `A`: any column on the target line (appends at end-of-line)
  - `i`: exact insertion column only (fallback)
- The mode-entry command determines the actual insert position independent of where in the range we land, so the final cursor position after typing + Esc is always `editResult.goalPos`.
- When navigating to the valid range, the `MotionBoundary` must use the full subset extent, not the target range (see `docs/optimizer/buffer-slicing.md` § Boundary vs Target Range).

