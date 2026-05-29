#include "Utils/OptimizerCaseCatalog.h"

#include <algorithm>

#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "Utils/SeedManager.h"

using namespace std;

const vector<NavCaseSpec>& navCaseCatalog() {
  static const vector<NavCaseSpec> cases = [] {
    vector<NavCaseSpec> c;
    NavOptimizerParams params;  // pinned default; shared by bench + explore

    auto point = [&](const string& name, int numLines, int avgLen,
                     BufferShape shape) {
      c.push_back({name, NavGoalKind::Point, numLines, avgLen, shape, 0, 0,
                   params});
    };

    for (int n : {1, 5, 10, 15, 20, 30})
      point("BufferSize/" + to_string(n), n, 30, BufferShape::CodeLike);
    for (int len : {10, 20, 40, 60, 80})
      point("LineLength/" + to_string(len), 20, len, BufferShape::CodeLike);
    point("BufferShape/Uniform", 20, 30, BufferShape::Uniform);
    point("BufferShape/Prose", 20, 30, BufferShape::Prose);
    point("BufferShape/CodeLike", 20, 30, BufferShape::CodeLike);

    for (int n : {5, 10, 20, 30, 40})
      c.push_back({"RangeBufferLines/" + to_string(n), NavGoalKind::Range, n, 30,
                   BufferShape::CodeLike, DEFAULT_RANGE_SIZE, 1, params});

    return c;
  }();
  return cases;
}

NavSetup buildNavSetup(const NavCaseSpec& spec, int seedIndex) {
  RandomGen::seed(SeedManager::instance().getSeed(seedIndex));

  NavSetup s;
  s.lines = generateBuffer(spec.numLines, spec.avgLen, spec.shape);
  s.goalKind = spec.goalKind;

  if (spec.goalKind == NavGoalKind::Point) {
    s.start = randomFirstPos(s.lines);
    s.goalPoint = randomLastPos(s.lines);
    CursorPos boundaryEnd(s.goalPoint.line, s.goalPoint.col + 1);
    s.boundary = NavBoundary(s.lines, s.start, boundaryEnd, true, true);
    return s;
  }

  // Range goal: tail range, mirroring RangeBenchmarkSetup in NavOptimizerBench.
  s.start = {0, 0};
  int lastLine = s.lines.lastLine();
  int lastLineLen = max(1, static_cast<int>(s.lines[lastLine].size()));
  int rangeLines = min(spec.rangeLines, lastLine + 1);
  if (rangeLines <= 1) {
    int actualChars = min(spec.rangeChars, lastLineLen);
    s.rangeBegin = {lastLine, max(0, lastLineLen - actualChars)};
    s.rangeEnd = {lastLine, lastLineLen};
  } else {
    int beginLine = lastLine - rangeLines + 1;
    int beginLineLen = max(1, static_cast<int>(s.lines[beginLine].size()));
    s.rangeBegin = {beginLine, max(0, beginLineLen / 2)};
    s.rangeEnd = {lastLine, lastLineLen};
  }
  s.boundary = NavBoundary(s.lines, s.rangeBegin, s.rangeEnd, true, true);
  return s;
}

const vector<EditCaseSpec>& editCaseCatalog() {
  static const vector<EditCaseSpec> cases = [] {
    vector<EditCaseSpec> c;
    auto del = [&](const string& name, int numLines, int avgLen) {
      c.push_back({name, EditGoalKind::PureDeletion, numLines, avgLen, {}});
    };
    for (int n : {1, 3, 5, 10, 15}) del("BufferSize/" + to_string(n), n, 30);
    for (int len : {10, 20, 40, 60}) del("LineLength/" + to_string(len), 5, len);
    for (int n : {2, 4, 6, 8, 10}) del("MultiLineDelete/" + to_string(n), n, 20);
    for (int n : {2, 4, 6})
      c.push_back({"MultiLineEdit/" + to_string(n), EditGoalKind::Transform, n, 25,
                   {"replacement"}});
    return c;
  }();
  return cases;
}

EditSetup buildEditSetup(const EditCaseSpec& spec, int seedIndex) {
  RandomGen::seed(SeedManager::instance().getSeed(seedIndex));

  EditSetup s;
  s.goalKind = spec.goalKind;
  s.initialLines = generateBuffer(spec.numLines, spec.avgLen, BufferShape::CodeLike);
  s.goalLines =
      spec.goalKind == EditGoalKind::PureDeletion ? Lines{""} : Lines(spec.goal);
  s.boundary =
      TransformBoundary(s.initialLines, CursorPos(0, 0), s.initialLines.endPos());
  s.params = TransformOptimizerParams{}.withMaxResults(
      max(10, s.initialLines.totalPositions() / 4));
  return s;
}

const vector<CompositionCaseSpec>& compositionCaseCatalog() {
  static const vector<CompositionCaseSpec> cases = [] {
    vector<CompositionCaseSpec> c;
    for (int e : {1, 2, 5, 8}) c.push_back({"EditCount/" + to_string(e), 15, 20, e});
    for (int n : {5, 10, 20, 40})
      c.push_back({"BufferSize/" + to_string(n), n, 20, min(5, n)});
    for (int len : {10, 20, 40, 60})
      c.push_back({"LineLength/" + to_string(len), 15, len, 5});
    return c;
  }();
  return cases;
}

CompositionSetup buildCompositionSetup(const CompositionCaseSpec& spec, int seedIndex) {
  RandomGen::seed(SeedManager::instance().getSeed(seedIndex));

  // Replace content on evenly-spaced edit lines with random words of similar
  // length, forcing a change when the draw collides with the original.
  CompositionSetup s;
  s.initial = generateBuffer(spec.numLines, spec.avgLen, BufferShape::CodeLike);
  s.goal = s.initial;
  for (int e = 0; e < spec.editCount; e++) {
    int line = spec.editCount <= 1
                   ? spec.numLines / 2
                   : e * (spec.numLines - 1) / max(1, spec.editCount - 1);
    int len = max(1, static_cast<int>(s.initial[line].size()));
    s.goal[line] = randomWord(len);
    if (s.goal[line] == s.initial[line]) s.goal[line] = "changed";
  }
  return s;
}
