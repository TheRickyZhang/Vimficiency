# Multi-Source A* State Merging - Exploration Notes

## Recording EditResult Answer
Ideally, like in MotionOptimizer, we simply record answer when a goal state is popped from the stack (guaranteed lowest cost). But we have a wrinkle with delete -> change conversions, as we would need to adjust in advance.

Several methods keeping an inverted order were tried, but in the end, guaranteed correctness is worth checking for a goal state twice. It may be possible to add a bool isGoal to trade memory in state for a faster branch check.

### Maintaining EditBoundary

### Some searches get starved (Not adequately explored)
Because of inadmissible heuristic, may not ever consider some branches

## Hashing lines in EditOpitmizer

## GoalSuffix
Beneficial to reuse results. With improvements to goal reach correctness and buffer hash, it is much faster.

