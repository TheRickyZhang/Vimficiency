#pragma once

// The planner's flat coordinate system and the two cost oracles it measures
// with: counted-command tilings of initial spans (TilingCost) and insert
// effort over goal prefixes (Typing). Internal to the VimDiff planner; see
// dev/optimizer/diff-generation.md § Tiled command-cost oracle.

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Types/Lines.h"

namespace VimDiff {

constexpr double INF = std::numeric_limits<double>::max() / 4.0;

// Unit u is [starts[u], starts[u+1]); both lists end with N.
struct FlatText {
  std::string text;
  std::vector<int> lineStarts, paraStarts;

  explicit FlatText(const Lines& lines);
};

enum Level { CHAR, WORD, BIG_WORD, LINE, PARAGRAPH, LEVEL_COUNT };
constexpr double MOVE_KEYS[LEVEL_COUNT] = {1, 1, 2, 1, 2};    // l, w, W, j, }
constexpr double DELETE_KEYS[LEVEL_COUNT] = {1, 2, 3, 2, 3};  // x, dw, dW, dd, dap
constexpr double TO_LINE_END_KEYS = 2.0;                      // D, $
constexpr double TO_LINE_START_KEYS = 2.0;                    // d0

template<CountClass C>
double countPrefixCost(int k, double scale) {
  if (k <= 1) return 0.0;
  return (int)std::to_string(k).size() * scale + runtimeCountPenalty<C>({k, k});
}

// Cheapest counted-command tiling of raw initial spans.
class TilingCost {
public:
  enum class Kind { Delete, Move };

  TilingCost(const FlatText& initial, double scale, int maxPrefixCount, Kind kind);

  // Upper bound on tiling one line's segment [x,y).
  double coverBound(int x, int y) const;
  double chainCost(Level level, int units) const;
  // Worst extra cost to end a tiling at `edge` instead of crossing it.
  double stopSlack(int edge) const;
  double startSlack(int edge) const;
  double query(int begin, int end);

  template<class Sink>
  void sweep(int begin, int end, Sink&& sink) {
    static const double ZERO = 0.0;
    sweep(ScalarOps{}, scalar_, begin, end,
          [&](int ri) -> const double& { return ri == begin ? ZERO : INF; },
          [&](int ri, const double& cost) { sink(ri, cost); });
  }

  // Run-start slots also hold sources inside the run (span-local rule).
  template<class V>
  struct Scratch {
    std::vector<V> reach, wordStart, bigStart, wsStart, lineMin, paraMin;
  };

  template<class V>
  Scratch<V> makeScratch(const V& inf) const {
    Scratch<V> s;
    for (std::vector<V>* v : {&s.reach, &s.wordStart, &s.bigStart, &s.wsStart})
      v->assign(N_ + 1, inf);
    s.lineMin.assign(lineStarts_.size(), inf);
    s.paraMin.assign(paraStarts_.size(), inf);
    return s;
  }

  struct ScalarOps {
    using V = double;
    const V& inf() const { return INF; }
    void reset(V& v) const { v = INF; }
    void relax(V& acc, const V& base, double add) const { acc = std::min(acc, base + add); }
  };

  // Multi-source; only slots written or reset in this sweep are read.
  template<class Ops, class Src, class Sink>
  void sweep(const Ops& ops, Scratch<typename Ops::V>& s, int begin, int end, Src&& src,
             Sink&& sink) {
    using V = typename Ops::V;
    if (begin >= end) return;
    s.reach[begin] = src(begin);
    V lineMin = s.reach[begin], paraMin = s.reach[begin];  // where j/} may start
    int lineStart = lineStartOf_[begin];
    if (isWord_[begin]) ops.reset(s.wordStart[wordStarts_[wordIdx_[begin]]]);
    if (isBig_[begin]) ops.reset(s.bigStart[bigStarts_[bigIdx_[begin]]]);
    if (isWs_[begin]) ops.reset(s.wsStart[wsRunStart_[begin]]);

    V e = ops.inf();
    for (int ri = begin + 1; ri <= end; ri++) {
      const int last = ri - 1;
      if (isWord_[last]) ops.relax(s.wordStart[wordStarts_[wordIdx_[last]]], src(last), 0.0);
      if (isBig_[last]) ops.relax(s.bigStart[bigStarts_[bigIdx_[last]]], src(last), 0.0);
      if (isWs_[last]) ops.relax(s.wsStart[wsRunStart_[last]], src(last), 0.0);
      if (lineIdx_[ri] >= 0) {
        s.lineMin[lineIdx_[ri] - 1] = lineMin;
        ops.reset(lineMin);
        lineStart = ri;
      }
      if (paraIdx_[ri] >= 0) {
        s.paraMin[paraIdx_[ri] - 1] = paraMin;
        ops.reset(paraMin);
      }
      ops.reset(e);
      for (int k = 1; k <= cap_ && ri - k >= begin; k++)
        ops.relax(e, s.reach[ri - k], chunk_[CHAR].cost(k));
      relaxRuns(ops, e, begin, ri, chunk_[WORD], isWord_, wordIdx_, wordStarts_, wordEnds_,
                s.wordStart, s.wsStart);
      relaxRuns(ops, e, begin, ri, chunk_[BIG_WORD], isBig_, bigIdx_, bigStarts_, bigEnds_,
                s.bigStart, s.wsStart);
      relaxUnits(ops, e, begin, ri, chunk_[LINE], lineIdx_, lineStarts_, s.reach, s.lineMin);
      relaxUnits(ops, e, begin, ri, chunk_[PARAGRAPH], paraIdx_, paraStarts_, s.reach, s.paraMin);
      if (ri == N_ || isNewline_[ri]) ops.relax(e, lineMin, toLineEnd_);  // D / $
      if (kind_ == Kind::Delete && lineStart >= begin && ri > lineStart)  // d0
        ops.relax(e, s.reach[lineStart], toLineStart_);
      sink(ri, e);
      std::swap(s.reach[ri], e);
      ops.relax(s.reach[ri], src(ri), 0.0);
      if (ri < N_) {
        if (isWord_[ri] && wordStarts_[wordIdx_[ri]] == ri) s.wordStart[ri] = s.reach[ri];
        if (isBig_[ri] && bigStarts_[bigIdx_[ri]] == ri) s.bigStart[ri] = s.reach[ri];
        if (isWs_[ri] && wsRunStart_[ri] == ri) s.wsStart[ri] = s.reach[ri];
      }
      ops.relax(lineMin, s.reach[ri], 0.0);
      ops.relax(paraMin, s.reach[ri], 0.0);
    }
  }

private:
  struct Chunk {
    double base = 0.0;
    double gap = 0.0;  // extra cost of splitting one {k}cmd in two
    std::vector<double> pen;
    double cost(int k) const { return base + pen[k]; }
  };

  double maxChunkGap() const;
  int lineOf(int ri) const;
  int paraOf(int ri) const;

  int N_;
  int cap_;  // CostOptions::maxPrefixCount
  Kind kind_;
  std::array<Chunk, LEVEL_COUNT> chunk_;
  double toLineEnd_, toLineStart_;
  std::vector<char> isWord_, isBig_, isWs_, isNewline_;
  std::vector<int> lineStartOf_;
  std::vector<int> wsRunStart_;
  std::vector<int> wordStarts_, wordEnds_, bigStarts_, bigEnds_;
  std::vector<int> wordIdx_, bigIdx_;
  std::vector<int> lineStarts_, paraStarts_;
  std::vector<int> lineIdx_, paraIdx_;
  Scratch<double> scalar_;

  void buildBoundaries(const std::string& text);

  template<CountClass Del, CountClass Mov>
  void buildChunk(Level level, double scale) {
    Chunk& c = chunk_[level];
    c.base = (kind_ == Kind::Delete ? DELETE_KEYS[level] : MOVE_KEYS[level]) * scale;
    c.pen.assign(cap_ + 1, 0.0);
    for (int k = 2; k <= cap_; k++)
      c.pen[k] = kind_ == Kind::Delete ? countPrefixCost<Del>(k, scale)
                                       : countPrefixCost<Mov>(k, scale);
  }

  // de-shape (ri-1 in a run) or dw-shape (ri-1 trailing ws).
  template<class Ops, class V>
  void relaxRuns(const Ops& ops, V& e, int begin, int ri, const Chunk& chunk,
                 const std::vector<char>& isClass, const std::vector<int>& idx,
                 const std::vector<int>& starts, const std::vector<int>& ends,
                 const std::vector<V>& runStart, const std::vector<V>& wsStart) const {
    const int last = ri - 1;
    int runIdx;
    bool endsInWs = false;
    if (isClass[last]) {
      runIdx = idx[last];
    } else if (isWs_[last] && wsRunStart_[last] > 0 && isClass[wsRunStart_[last] - 1]) {
      runIdx = idx[wsRunStart_[last] - 1];
      endsInWs = true;
    } else {
      return;
    }
    for (int k = 1; k <= cap_ && runIdx - k + 1 >= 0; k++) {
      const int run = runIdx - k + 1;
      if (ends[run] <= begin) break;
      const int runBegin = starts[run];
      ops.relax(e, runStart[runBegin], chunk.cost(k));
      if (runBegin > begin && isWs_[runBegin - 1] && k + endsInWs <= cap_)
        ops.relax(e, wsStart[wsRunStart_[runBegin - 1]], chunk.cost(k + endsInWs));
    }
  }

  template<class Ops, class V>
  void relaxUnits(const Ops& ops, V& e, int begin, int ri, const Chunk& chunk,
                  const std::vector<int>& idxAt, const std::vector<int>& starts,
                  const std::vector<V>& reach, const std::vector<V>& unitMin) const {
    if (idxAt[ri] < 0) return;
    const int unit = idxAt[ri];
    for (int u = unit - 1; u >= 0 && unit - u <= cap_; u--) {
      if (kind_ == Kind::Delete) {
        if (starts[u] < begin) break;
        ops.relax(e, reach[starts[u]], chunk.cost(unit - u));
      } else {
        if (starts[u + 1] <= begin) break;
        ops.relax(e, unitMin[u], chunk.cost(unit - u));
      }
    }
  }
};

// Typing raw goal [begin,end) costs PS[end] - PS[begin] - cut[begin].
struct Typing {
  std::vector<double> PS, cut;
  double entry, esc;  // the `i` and `<Esc>` keystrokes

  Typing(const FlatText& goal, const Config& config);

  double ins(int begin, int end) const { return PS[end] - PS[begin] - cut[begin]; }
};

}  // namespace VimDiff
