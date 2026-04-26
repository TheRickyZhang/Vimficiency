#include "CompositionStepArtifacts.h"

#include <algorithm>
#include <cassert>
#include <tuple>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Utils/StringUtils.h"
#include "VimCore/VimEditUtils.h"

using namespace std;

namespace {

CursorPos computeInsertEndPos(CursorPos insertPos, const string& insertedText) {
  if (insertedText.empty()) return insertPos;
  Lines inserted = Lines::unflatten(insertedText);
  if (inserted.size() == 1) {
    int endCol = insertPos.col + static_cast<int>(inserted[0].size()) - 1;
    return CursorPos(insertPos.line, max(0, endCol));
  }
  int lastLine = insertPos.line + static_cast<int>(inserted.size()) - 1;
  int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
  return CursorPos(lastLine, lastCol);
}

tuple<int, int, bool> findMatchingQuotePair(
    const string& line,
    int beginCol,
    int endCol,
    char quote) {
  vector<int> quotePositions;
  for (int i = 0; i < static_cast<int>(line.size()); i++) {
    if (line[i] == quote) quotePositions.push_back(i);
  }

  for (size_t i = 0; i + 1 < quotePositions.size(); i += 2) {
    int open = quotePositions[i];
    int close = quotePositions[i + 1];
    if (open + 1 == beginCol && close == endCol) return {open, close, false};
    if (open == beginCol && close + 1 == endCol) return {open, close, true};
  }
  return {-1, -1, false};
}

tuple<int, int, bool> findMatchingBracketPair(
    const string& line,
    int beginCol,
    int endCol,
    char open,
    char close) {
  int bestOpen = -1;
  int bestClose = -1;
  bool bestIsAround = false;

  vector<int> openStack;
  for (int i = 0; i < static_cast<int>(line.size()); i++) {
    if (line[i] == open) {
      openStack.push_back(i);
    } else if (line[i] == close && !openStack.empty()) {
      int openPos = openStack.back();
      openStack.pop_back();

      if (openPos + 1 == beginCol && i == endCol &&
          (bestOpen == -1 || openPos > bestOpen)) {
        bestOpen = openPos;
        bestClose = i;
        bestIsAround = false;
      }

      if (openPos == beginCol && i + 1 == endCol &&
          (bestOpen == -1 || openPos > bestOpen)) {
        bestOpen = openPos;
        bestClose = i;
        bestIsAround = true;
      }
    }
  }

  return {bestOpen, bestClose, bestIsAround};
}

void scanQuotesForEdit(
    BracketQuoteContext& ctx,
    const string& line,
    int beginCol,
    int endCol) {
  int lineLen = static_cast<int>(line.size());
  if (lineLen == 0) return;

  ctx.validQuoteMask.resize(lineLen);

  for (char quote : {'"', '\'', '`'}) {
    auto [openCol, closeCol, isAround] =
        findMatchingQuotePair(line, beginCol, endCol, quote);
    if (openCol == -1 || closeCol >= lineLen) continue;

    if (isAround) ctx.useAroundQuote.add(quote);

    int firstQuoteOfType = -1;
    for (int col = 0; col < lineLen; col++) {
      if (line[col] == quote) {
        firstQuoteOfType = col;
        break;
      }
    }

    if (firstQuoteOfType == openCol) {
      for (int col = 0; col <= closeCol; col++) {
        ctx.validQuoteMask[col].add(quote);
      }
    } else {
      for (int col = openCol; col <= closeCol; col++) {
        ctx.validQuoteMask[col].add(quote);
      }
    }
  }
}

void scanBracketsForEdit(
    BracketQuoteContext& ctx,
    const string& line,
    int beginCol,
    int endCol) {
  int lineLen = static_cast<int>(line.size());
  if (lineLen == 0) return;

  ctx.validBracketMask.resize(lineLen);

  for (auto [open, close] :
       vector<pair<char, char>>{{'(', ')'}, {'[', ']'}, {'{', '}'}, {'<', '>'}}) {
    auto [openCol, closeCol, isAround] =
        findMatchingBracketPair(line, beginCol, endCol, open, close);
    if (openCol == -1 || openCol >= lineLen || closeCol >= lineLen) continue;

    if (isAround) ctx.useAroundBracket.add(open);

    vector<int> firstOpenForward(lineLen, -1);
    int nextOpen = -1;
    for (int col = lineLen - 1; col >= 0; col--) {
      if (line[col] == open) nextOpen = col;
      firstOpenForward[col] = nextOpen;
    }

    int balance = 0;
    for (int col = 0; col < lineLen; col++) {
      bool insidePair = (col >= openCol && col <= closeCol);
      bool forwardReachesPair =
          (col < openCol && balance == 0 && firstOpenForward[col] == openCol);
      if (insidePair || forwardReachesPair) {
        ctx.validBracketMask[col].add(open);
      }
      if (line[col] == open) balance++;
      else if (line[col] == close) balance--;
    }
  }
}

struct JoinSimulation {
  string joinedLine;
  vector<int> cursorCols;

  static JoinSimulation simulate(const Lines& srcLines, int begin, int end) {
    JoinSimulation sim;
    Lines workLines(srcLines.begin() + begin, srcLines.begin() + end);
    CursorPos pos(0, 0);

    for (int l = begin + 1; l < end; l++) {
      VimCore::joinLines(workLines, pos, /*addSpace=*/true);
      sim.cursorCols.push_back(pos.col);
    }
    sim.joinedLine = workLines[0];
    return sim;
  }
};

int commonPrefixLen(string_view a, string_view b) {
  int n = static_cast<int>(min(a.size(), b.size()));
  for (int i = 0; i < n; i++) {
    if (a[i] != b[i]) return i;
  }
  return n;
}

int commonSuffixLen(string_view a, string_view b, int prefixLen) {
  int la = static_cast<int>(a.size());
  int lb = static_cast<int>(b.size());
  int maxSuffix = min(la, lb) - prefixLen;
  int count = 0;
  for (int i = 0; i < maxSuffix; i++) {
    if (a[la - 1 - i] != b[lb - 1 - i]) break;
    count++;
  }
  return count;
}

}

TransformResult computeTransformResultForDiff(
    const DiffState& diff,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored) {
  if (nodesExplored) *nodesExplored = 0;

  TransformOptimizer transformOptimizer(config);

  if (diff.isPureInsertion()) {
    Lines insertLines = Lines::unflatten(diff.insertedText);
    KeyedSequence full = KeyedSequence::i;
    full += buildTypedCommands(insertLines);
    RunningEffort runningEffort(full.keys, config);
    double effort = runningEffort.getEffort(config);

    vector<vector<Result>> insertResultsByStart(1);
    insertResultsByStart[0].emplace_back(std::move(full.seq), effort);

    CursorPos goalPos = computeInsertEndPos(diff.beginPos, diff.insertedText);
    Lines singlePoint = {""};
    return TransformResult(std::move(insertResultsByStart), {}, singlePoint,
                      diff.beginPos.line, diff.beginPos.col, goalPos);
  }

  TransformOptimizerParams editParams =
      TransformOptimizerParams{}
          .withMinCountRepeat(params.minPrefixCount)
          .withMaxCountRepeat(params.maxPrefixCount)
          .withMaxMultiplePerStartPosition(params.maxTransformResultsPerPosition);

  if (diff.isPureDeletion()) {
    TransformResult result = transformOptimizer.optimizePureDeletion(
        diff.deletedLines(), diff.boundary, editParams,
        diff.beginPos.line, diff.beginPos.col, diff.beginPos);
    if (nodesExplored) *nodesExplored = result.getStats().nodesExplored();
    return result;
  }

  CursorPos goalPos;
  const Lines& inserted = diff.insertedLines();
  if (inserted.empty() || (inserted.size() == 1 && inserted[0].empty())) {
    goalPos = diff.beginPos;
  } else if (inserted.size() == 1) {
    goalPos = CursorPos(
        diff.beginPos.line,
        diff.beginPos.col + static_cast<int>(inserted[0].size()) - 1);
  } else {
    int lastLine = diff.beginPos.line + static_cast<int>(inserted.size()) - 1;
    int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
    goalPos = CursorPos(lastLine, lastCol);
  }

  TransformResult result = transformOptimizer.optimizeTransform(
      diff.deletedLines(), diff.insertedLines(), diff.boundary, editParams,
      diff.beginPos.line, diff.beginPos.col, goalPos);
  if (nodesExplored) *nodesExplored = result.getStats().nodesExplored();
  return result;
}

BracketQuoteContext computeTextObjectContextForDiff(
    const DiffState& diff,
    const Lines& preEditLines) {
  BracketQuoteContext ctx;

  if (diff.isPureInsertion()) return ctx;
  if (diff.beginPos.line != diff.endPos.line) return ctx;
  if (diff.beginPos.line >= static_cast<int>(preEditLines.size())) return ctx;

  const string& line = preEditLines[diff.beginPos.line];
  ctx.line = diff.beginPos.line;
  scanQuotesForEdit(ctx, line, diff.beginPos.col, diff.endPos.col);
  scanBracketsForEdit(ctx, line, diff.beginPos.col, diff.endPos.col);
  return ctx;
}

optional<JoinPlan> computeJoinPlanForDiff(
    const DiffState& diff,
    const Lines& preEditLines,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored) {
  if (nodesExplored) *nodesExplored = 0;
  if (diff.isPureInsertion() || diff.isPureDeletion()) return nullopt;

  TransformOptimizer transformOptimizer(config);

  int srcFirstLine = diff.beginPos.line;
  Lines delLines = diff.deletedLines();
  int srcEndLine = srcFirstLine + static_cast<int>(delLines.size());
  if (srcEndLine > static_cast<int>(preEditLines.size())) return nullopt;

  int sourceLineCount = srcEndLine - srcFirstLine;
  Lines targetLines = diff.insertedLines();
  int targetLineCount = static_cast<int>(targetLines.size());
  if (sourceLineCount <= targetLineCount) return nullopt;

  const string& prefix = diff.boundary.prefix();
  const string& suffix = diff.boundary.suffix();
  Lines srcLines = preEditLines.getLineRange(srcFirstLine, srcEndLine);

  Lines fullTargetLines;
  for (int t = 0; t < targetLineCount; t++) {
    string line = targetLines[t];
    if (t == 0 && targetLineCount == 1) {
      line = prefix + line + suffix;
    } else if (t == 0) {
      line = prefix + line;
    } else if (t == targetLineCount - 1) {
      line = line + suffix;
    }
    fullTargetLines.push_back(std::move(line));
  }

  vector<pair<int, int>> partition(targetLineCount);
  if (targetLineCount == 1) {
    partition[0] = {0, sourceLineCount};
  } else {
    vector<int> prefLen(sourceLineCount + 1, 0);
    for (int s = 0; s < sourceLineCount; s++) {
      prefLen[s + 1] = prefLen[s] + static_cast<int>(srcLines[s].size());
    }
    auto joinedLen = [&](int a, int b) -> int {
      return prefLen[b + 1] - prefLen[a] + (b - a);
    };

    constexpr int INF = 1000000;
    vector<vector<int>> dp(sourceLineCount + 1, vector<int>(targetLineCount + 1, INF));
    vector<vector<int>> choice(sourceLineCount + 1, vector<int>(targetLineCount + 1, -1));
    dp[sourceLineCount][targetLineCount] = 0;

    for (int t = targetLineCount - 1; t >= 0; t--) {
      int targetLen = static_cast<int>(fullTargetLines[t].size());
      for (int s = sourceLineCount - (targetLineCount - t); s >= t; s--) {
        for (int k = s; k <= sourceLineCount - (targetLineCount - t); k++) {
          int groupCost = abs(joinedLen(s, k) - targetLen);
          int total = groupCost + dp[k + 1][t + 1];
          if (total < dp[s][t]) {
            dp[s][t] = total;
            choice[s][t] = k;
          }
        }
      }
    }

    int s = 0;
    for (int t = 0; t < targetLineCount; t++) {
      int k = choice[s][t];
      assert(k != -1 && "DP partition reconstruction visited an unreachable state");
      partition[t] = {s, k + 1};
      s = k + 1;
    }
  }

  for (int g = 0; g < targetLineCount; g++) {
    auto [begin, end] = partition[g];
    if (end - begin <= 1) continue;

    auto sim = JoinSimulation::simulate(srcLines, begin, end);
    int cpLen = commonPrefixLen(sim.joinedLine, fullTargetLines[g]);
    int csLen = commonSuffixLen(sim.joinedLine, fullTargetLines[g], cpLen);
    int commonLen = cpLen + csLen;
    int maxLen = max(static_cast<int>(sim.joinedLine.size()),
                     static_cast<int>(fullTargetLines[g].size()));
    double matchRatio = maxLen > 0 ? static_cast<double>(commonLen) / maxLen : 1.0;
    if (matchRatio < 0.3) return nullopt;
  }

  Sequence fullSeq;
  CursorPos lastGoalPos(0, 0);

  for (int g = 0; g < targetLineCount; g++) {
    auto [begin, end] = partition[g];
    int numJoins = end - begin - 1;

    if (g > 0) fullSeq.append("j");
    for (int j = 0; j < numJoins; j++) fullSeq.append("J");

    auto sim = JoinSimulation::simulate(srcLines, begin, end);
    int cursorCol = numJoins > 0 ? sim.cursorCols.back() : 0;

    if (sim.joinedLine != fullTargetLines[g]) {
      Lines residualInitial = {sim.joinedLine};
      TransformBoundary groupBoundary;
      CursorPos residualGoalPos(
          0, fullTargetLines[g].empty() ? 0 : static_cast<int>(fullTargetLines[g].size()) - 1);
      TransformOptimizerParams residualParams =
          TransformOptimizerParams{}
              .withMinCountRepeat(params.minPrefixCount)
              .withMaxCountRepeat(params.maxPrefixCount);

      TransformResult residualResult = [&]() -> TransformResult {
        if (fullTargetLines[g].empty()) {
          return transformOptimizer.optimizePureDeletion(
              residualInitial, groupBoundary, residualParams, 0, 0, residualGoalPos);
        }
        Lines residualGoal = {fullTargetLines[g]};
        return transformOptimizer.optimizeTransform(
            residualInitial, residualGoal, groupBoundary, residualParams, 0, 0, residualGoalPos);
      }();

      if (nodesExplored) *nodesExplored += residualResult.getStats().nodesExplored();

      const Result* res = residualResult.resultAt(0, cursorCol);
      if (!res) return nullopt;

      fullSeq.append(res->getSequence().view());
      lastGoalPos = residualResult.getGoalPos();
    } else {
      lastGoalPos = CursorPos(0, cursorCol);
    }
  }

  CursorPos goalPos;
  if (targetLineCount == 1) {
    goalPos = CursorPos(diff.beginPos.line, lastGoalPos.col);
  } else {
    goalPos = CursorPos(diff.beginPos.line + targetLineCount - 1, lastGoalPos.col);
  }

  return JoinPlan{
      .sequence = std::move(fullSeq),
      .goalPos = goalPos,
      .effort = getEffort(fullSeq.view(), config),
      .entryLine = diff.beginPos.line,
  };
}
