#pragma once

#include <string>
#include <vector>

#include "Boundary/NavBoundary.h"
#include "Boundary/TransformBoundary.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/RandomBufferHelpers.h"

// Single source of truth for the optimizer cases that both the benchmark
// binary (vimfy_benchmarks) and the explore tool (vimfy_explore) run. The two
// can't share an execution — explore needs VIMF_TRACK_STATES (which ruins
// timing) — so they share the *inputs*: the bench times each case across
// DEFAULT_SEED_COUNT seeds, the explorer traces seed 0 of the same case.
//
// Parity lives in build*Setup, not the spec list: a setup's buffer is fixed by
// the exact RNG draw order (seed -> generateBuffer -> positions), so both
// callers must build through the same function. Config (Config::uniform()) and
// params are pinned by the spec/builder so the search itself can't drift.

enum class NavGoalKind { Point, Range };

struct NavCaseSpec {
  std::string name;        // dashboard category/case, e.g. "BufferSize/10"
  NavGoalKind goalKind;
  int numLines;
  int avgLen;
  BufferShape shape;
  int rangeChars;          // Range only: tail-range width (cols)
  int rangeLines;          // Range only: tail-range height (lines)
  NavOptimizerParams params;
};

struct NavSetup {
  Lines lines;
  NavBoundary boundary{};
  CursorPos start;
  NavGoalKind goalKind = NavGoalKind::Point;
  CursorPos goalPoint;              // Point goal
  CursorPos rangeBegin, rangeEnd;   // Range goal: half-open [begin, end) CharRange
};

// The explorable motion cases. Bench registers these (plus its own bench-only
// tuning sweeps); explore iterates exactly this list.
const std::vector<NavCaseSpec>& navCaseCatalog();

// Reproduces the bench's point/range setup construction for a given seed,
// preserving RNG order so the bench's seed-i buffer == the explorer's.
NavSetup buildNavSetup(const NavCaseSpec& spec, int seedIndex);

// --- Edit (transform) cases ----------------------------------------------

enum class EditGoalKind { PureDeletion, Transform };

struct EditCaseSpec {
  std::string name;
  EditGoalKind goalKind;
  int numLines;
  int avgLen;
  std::vector<std::string> goal;  // Transform only; pure deletion ignores it
};

struct EditSetup {
  Lines initialLines;
  Lines goalLines;                  // {""} for pure deletion
  TransformBoundary boundary;
  TransformOptimizerParams params;  // includes buffer-sized maxResults cap
  EditGoalKind goalKind = EditGoalKind::PureDeletion;
};

const std::vector<EditCaseSpec>& editCaseCatalog();
EditSetup buildEditSetup(const EditCaseSpec& spec, int seedIndex);

// --- Composition cases ---------------------------------------------------

struct CompositionCaseSpec {
  std::string name;
  int numLines;
  int avgLen;
  int editCount;
};

struct CompositionSetup {
  Lines initial;
  Lines goal;
  CompositionOptimizerParams params;  // pinned default; shared by bench + explore
};

const std::vector<CompositionCaseSpec>& compositionCaseCatalog();
CompositionSetup buildCompositionSetup(const CompositionCaseSpec& spec, int seedIndex);
