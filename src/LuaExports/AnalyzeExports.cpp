#include "LuaExports/Common.h"
#include "LuaExports/AsyncJob.h"
#include "LuaExports/AsyncJobRegistry.h"
#include "Boundary/NavBoundary.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Optimizer/SearchControl.h"
#include "Utils/Debug.h"
#include <algorithm>
#include <iomanip>
#include <memory>
#include <string>
#include <utility>

using namespace std;
namespace helpers = VF::LuaExports::helpers;
namespace payload = VF::LuaExports::payload;
using VF::LuaExports::g_config_internal;
using VF::LuaExports::EVENT_FIELD_SEP;
using VF::LuaExports::AsyncJob;
using VF::LuaExports::JobRegistry;

namespace {

VF::LuaExports::Result<OptimizerParamOverrides> parseOverrides(
    string_view text) {
  auto parsed = parseOptimizerParamOverrides(text);
  if (!parsed) {
    return helpers::unexpectedError(
        VF::LuaExports::ExportErrorKind::InvalidValue,
        formatOptimizerParamOverrideErrors(parsed.error()));
  }
  return std::move(*parsed);
}

// Fully-owning decode of the analyze FFI inputs. Owns its Lines and keyseq so it
// can outlive the FFI buffers on the async worker thread (same ownership rule as
// StartInputs in InteractiveExports.cpp). `overrides` is kept un-resolved
// because analyze branches on initial==goal and applies them to different param
// types (Nav vs Composition).
struct AnalyzeInputs {
  Lines initialLines;
  Lines goalLines;
  CursorPos initialPos{0, 0};
  CursorPos goalPos{0, 0};
  NavBoundary boundary;
  NavContext navContext;
  string keyseqText;
  OptimizerParamOverrides overrides;
  int resultsCalculated = 0;
};

VF::LuaExports::Result<AnalyzeInputs> decodeAnalyzeInputs(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int boundaryFirstCol,
    int boundaryLastCol,
    bool hasLinesAbove,
    bool hasLinesBelow,
    int start_row,
    int start_col,
    int end_row,
    int end_col,
    const char *keyseq,
    size_t keyseq_len,
    int window_height,
    int scroll_amount,
    int results_calculated) {
  CHECK(results_calculated >= 0, "results_calculated must be non-negative");
  auto initialText = helpers::requiredBytes(
      encoded_initial_lines,
      encoded_initial_lines_len,
      "encoded_initial_lines");
  if (!initialText) return unexpected(initialText.error());
  auto goalText = helpers::requiredBytes(
      encoded_goal_lines,
      encoded_goal_lines_len,
      "encoded_goal_lines");
  if (!goalText) return unexpected(goalText.error());
  auto keyseqTextResult = helpers::requiredBytes(keyseq, keyseq_len, "keyseq");
  if (!keyseqTextResult) return unexpected(keyseqTextResult.error());

  auto initialLinesResult = payload::decodeLineArray(*initialText);
  if (!initialLinesResult) return unexpected(initialLinesResult.error());
  auto goalLinesResult = payload::decodeLineArray(*goalText);
  if (!goalLinesResult) return unexpected(goalLinesResult.error());

  AnalyzeInputs in;
  in.initialLines = std::move(*initialLinesResult);
  in.goalLines = std::move(*goalLinesResult);
  in.keyseqText.assign(*keyseqTextResult);
  in.resultsCalculated = results_calculated;

  // Neovim can capture an insert-mode / 'virtualedit' cursor one past
  // end-of-line, or (for recall windows) a row outside the sliced buffer. The
  // optimizer requires normal-mode-valid endpoints, so clamp at the boundary
  // rather than aborting on the downstream CHECK.
  in.initialPos = in.initialLines.clamp(CursorPos(start_row, start_col));
  in.goalPos = in.goalLines.clamp(CursorPos(end_row, end_col));
  in.navContext = NavContext(window_height, scroll_amount);
  in.boundary = NavBoundary(
      in.initialLines,
      CursorPos(0, boundaryFirstCol),
      CursorPos(static_cast<int>(in.initialLines.size()) - 1, boundaryLastCol + 1),
      hasLinesAbove,
      hasLinesBelow);
  return in;
}

// The heavy half: the Nav-vs-Composition branch plus result-string formatting.
// Runs on the calling thread for `vf_analyze` and on a worker for the async
// trio. `control` bounds whichever search runs.
string runAnalyze(
    const AnalyzeInputs& in, const Config& config, const SearchControl* control) {
  vector<::Result> results;

  if (in.initialLines == in.goalLines) {
    // NavOptimizer returns LandingResult (with goalPos); for analyze we only
    // surface (sequence, cost), so down-convert to Result for shared output.
    NavOptimizer opt(config);
    NavOptimizerParams navParams = NavOptimizerParams{}
        .withMaxResults(in.resultsCalculated)
        .withMaxResultsPerEndPos(2);
    in.overrides.applyTo(navParams);
    auto navResults = opt.optimize(
        in.initialLines,
        in.initialPos,
        in.goalPos,
        navParams,
        in.keyseqText,
        in.boundary,
        in.navContext,
        control).getResults();
    results.reserve(navResults.size());
    for (const auto& r : navResults) {
      results.emplace_back(r.getSequence(), r.getCost());
    }
  } else {
    CompositionOptimizer opt(config);
    CompositionOptimizerParams compParams =
        CompositionOptimizerParams{}.withMaxResults(in.resultsCalculated);
    in.overrides.applyTo(compParams);
    results = opt.optimize(
        in.initialLines,
        in.initialPos,
        in.goalLines,
        in.goalPos,
        compParams,
        in.keyseqText,
        in.boundary,
        in.navContext,
        control).getResults();
  }

  const double userCost = getEffort(in.keyseqText, config);
  vector<const ::Result*> validResults;
  for (const ::Result& result : results) {
    if (result.getSequence().empty()) continue;
    validResults.push_back(&result);
  }

  sort(validResults.begin(), validResults.end(),
            [](const ::Result* a, const ::Result* b) { return a->getCost() < b->getCost(); });

  ostringstream oss;
  if (validResults.empty()) {
    oss << "no results";
  } else {
    oss << "size: " << validResults.size() << " user_cost: "
        << fixed << setprecision(3) << userCost << "\n";
    for (const ::Result* result : validResults) {
      // Machine-readable export for the Lua bridge: use the raw sequence
      // bytes, not Sequence's human formatter, so insert-mode text and
      // <Esc> survive round-tripping through ffi.lua's line parser.
      // Field separator is the shared `EVENT_FIELD_SEP` (0x1F Unit Sep);
      // rationale and the project-wide convention live in
      // `dev/lua/ffi-separators.md`.
      oss << result->getSequence().view()
          << EVENT_FIELD_SEP
          << fixed << setprecision(3) << result->getCost() << "\n";
    }
  }

  if constexpr (DEBUG_ENABLED) {
    oss << "\n ----------------DEBUG---------------- \n" << consume_debug_output();
  }
  return oss.str();
}

// Background analyze search. Owns its inputs and a Config snapshot so the worker
// is independent of the FFI buffers and the main thread. The result string is
// read once `vf_analyze_poll` observes `ready()`.
struct AnalyzeJob : AsyncJob {
  AnalyzeInputs inputs;
  Config config;
  string result;

  AnalyzeJob(AnalyzeInputs in, Config cfg, int deadlineMs)
      : AsyncJob(deadlineMs), inputs(std::move(in)), config(std::move(cfg)) {}

  void start() {
    spawn([this] { result = runAnalyze(inputs, config, control()); });
  }
};

JobRegistry<AnalyzeJob> g_analyze_jobs;

VF::LuaExports::Result<string> analyzeImpl(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int boundaryFirstCol,
    int boundaryLastCol,
    bool hasLinesAbove,
    bool hasLinesBelow,
    int start_row,
    int start_col,
    int end_row,
    int end_col,
    const char *keyseq,
    size_t keyseq_len,
    int window_height,
    int scroll_amount,
    int results_calculated,
    const char *optimizer_overrides,
    size_t optimizer_overrides_len) {
  auto overridesText = helpers::requiredBytes(
      optimizer_overrides,
      optimizer_overrides_len,
      "optimizer_overrides");
  if (!overridesText) return unexpected(overridesText.error());
  auto overridesResult = parseOverrides(*overridesText);
  if (!overridesResult) return unexpected(overridesResult.error());

  auto inputsRes = decodeAnalyzeInputs(
      encoded_initial_lines, encoded_initial_lines_len,
      encoded_goal_lines, encoded_goal_lines_len,
      boundaryFirstCol, boundaryLastCol, hasLinesAbove, hasLinesBelow,
      start_row, start_col, end_row, end_col,
      keyseq, keyseq_len, window_height, scroll_amount, results_calculated);
  if (!inputsRes) return unexpected(inputsRes.error());
  inputsRes->overrides = std::move(*overridesResult);

  return runAnalyze(*inputsRes, g_config_internal, nullptr);
}

VF::LuaExports::Result<string> startAsyncImpl(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int boundaryFirstCol,
    int boundaryLastCol,
    bool hasLinesAbove,
    bool hasLinesBelow,
    int start_row,
    int start_col,
    int end_row,
    int end_col,
    const char *keyseq,
    size_t keyseq_len,
    int window_height,
    int scroll_amount,
    int results_calculated,
    const char *optimizer_overrides,
    size_t optimizer_overrides_len,
    int deadline_ms) {
  auto overridesText = helpers::requiredBytes(
      optimizer_overrides,
      optimizer_overrides_len,
      "optimizer_overrides");
  if (!overridesText) return unexpected(overridesText.error());
  auto overridesResult = parseOverrides(*overridesText);
  if (!overridesResult) return unexpected(overridesResult.error());

  auto inputsRes = decodeAnalyzeInputs(
      encoded_initial_lines, encoded_initial_lines_len,
      encoded_goal_lines, encoded_goal_lines_len,
      boundaryFirstCol, boundaryLastCol, hasLinesAbove, hasLinesBelow,
      start_row, start_col, end_row, end_col,
      keyseq, keyseq_len, window_height, scroll_amount, results_calculated);
  if (!inputsRes) return unexpected(inputsRes.error());
  inputsRes->overrides = std::move(*overridesResult);

  auto job = std::make_unique<AnalyzeJob>(
      std::move(*inputsRes), g_config_internal, deadline_ms);
  job->start();
  const int job_id = g_analyze_jobs.create(std::move(job));
  return to_string(job_id);
}

}  // namespace

extern "C" {

VFByteSlice vf_analyze(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int boundaryFirstCol,
    int boundaryLastCol,
    bool hasLinesAbove,
    bool hasLinesBelow,
    int start_row,
    int start_col,
    int end_row,
    int end_col,
    const char *keyseq,
    size_t keyseq_len,
    int window_height,
    int scroll_amount,
    int results_calculated,
    const char *optimizer_overrides,
    size_t optimizer_overrides_len) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      analyzeImpl(
          encoded_initial_lines,
          encoded_initial_lines_len,
          encoded_goal_lines,
          encoded_goal_lines_len,
          boundaryFirstCol,
          boundaryLastCol,
          hasLinesAbove,
          hasLinesBelow,
          start_row,
          start_col,
          end_row,
          end_col,
          keyseq,
          keyseq_len,
          window_height,
          scroll_amount,
          results_calculated,
          optimizer_overrides,
          optimizer_overrides_len));
}

// Kicks off the analyze search on a worker thread and returns a job-id string
// immediately. Poll with vf_analyze_poll until it stops returning "pending".
VFByteSlice vf_analyze_start_async(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int boundaryFirstCol,
    int boundaryLastCol,
    bool hasLinesAbove,
    bool hasLinesBelow,
    int start_row,
    int start_col,
    int end_row,
    int end_col,
    const char *keyseq,
    size_t keyseq_len,
    int window_height,
    int scroll_amount,
    int results_calculated,
    const char *optimizer_overrides,
    size_t optimizer_overrides_len,
    int deadline_ms) {
  static string storage;
  return helpers::storeBytes(
      storage,
      startAsyncImpl(
          encoded_initial_lines,
          encoded_initial_lines_len,
          encoded_goal_lines,
          encoded_goal_lines_len,
          boundaryFirstCol,
          boundaryLastCol,
          hasLinesAbove,
          hasLinesBelow,
          start_row,
          start_col,
          end_row,
          end_col,
          keyseq,
          keyseq_len,
          window_height,
          scroll_amount,
          results_calculated,
          optimizer_overrides,
          optimizer_overrides_len,
          deadline_ms));
}

// Returns "pending" while the worker runs; once done, returns the same result
// string vf_analyze would produce and frees the job.
VFByteSlice vf_analyze_poll(int job_id) {
  static string storage;
  AnalyzeJob& job = g_analyze_jobs.get(job_id);
  if (!job.ready()) {
    storage = "pending";
    return helpers::byteSlice(storage);
  }
  std::unique_ptr<AnalyzeJob> owned = g_analyze_jobs.take(job_id);
  storage = std::move(owned->result);
  return helpers::byteSlice(storage);
}

// Signals cancellation and frees the job; the jthread destructor joins the
// worker on this (main) thread. The flag is polled between composition setup
// phases and per pop in every search loop, so the join is bounded by one
// diff's precompute or one pop expansion.
int vf_analyze_cancel(int job_id) {
  std::unique_ptr<AnalyzeJob> owned = g_analyze_jobs.take(job_id);
  if (!owned) return 0;
  owned->cancel();
  return 1;
}

}
