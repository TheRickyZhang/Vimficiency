#pragma once

// The planner's flat coordinate system and the two cost oracles it measures
// with: counted-command tilings of initial spans (TilingCost) and per-key insert
// effort over goal prefixes (Typing). Internal to the VimDiff planner; see
// dev/optimizer/diff-generation.md § Tiled command-cost oracle.

#include <array>
#include <limits>
#include <string>
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

// The command one tiling chunk stands for. The first five alias Level; the last
// two are the uncounted line-edge commands (D/$ and d0).
enum ChunkKind : int8_t { CHUNK_CHAR, CHUNK_WORD, CHUNK_BIG_WORD, CHUNK_LINE, CHUNK_PARAGRAPH,
                          CHUNK_LINE_END, CHUNK_LINE_START };

// Cheapest tiling reaching position `at`, the raw position it started from, and
// the last chunk that got it there (`from` < 0 for a bare seed).
struct Arrival {
  double cost = INF;
  int seed = -1;
  int at = -1;
  int from = -1;
  ChunkKind kind = CHUNK_CHAR;
  int8_t count = 0;
};

struct TilingChunk {
  int begin, end;
  ChunkKind kind;
  int count;
  double cost;
};

// Cheapest counted-command tiling of raw initial spans.
class TilingCost {
public:
  enum class Kind { Delete, Move };

  TilingCost(const FlatText& initial, double scale, int mxcnt, Kind kind);

  // Upper bound on tiling one line's segment [x,y).
  double coverBound(int x, int y) const;
  double chainCost(Level level, int units) const;
  // Worst extra cost to end a tiling at `edge` instead of crossing it.
  double stopSlack(int edge) const;
  double startSlack(int edge) const;
  double query(int begin, int end);
  // The chunks of the cheapest tiling of [begin, end), in order.
  std::vector<TilingChunk> cover(int begin, int end);

  // Cheapest tiling from `begin` to every ri in (begin, end]: sink(ri, cost).
  template<class Sink>
  void sweep(int begin, int end, Sink&& sink) {
    sweep(
        begin, end, [&](int ri) { return ri == begin ? 0.0 : INF; },
        [&](int ri, const Arrival& a) { sink(ri, a.cost); });
  }

  // Multi-source: `seed(ri)` is the cost of a tiling starting at ri (INF for
  // none). sink(ri, arrival) reports the cheapest arrival at each ri in
  // (begin, end], excluding ri's own seed.
  template<class Seed, class Sink>
  void sweep(int begin, int end, Seed&& seed, Sink&& sink) {
    if (begin >= end) return;
    auto at = [&](int ri) { return Arrival{.cost = seed(ri), .seed = ri, .at = ri}; };
    reach_[begin] = at(begin);
    Arrival lineMin = reach_[begin], paraMin = reach_[begin];  // where j/} may start
    int lineStart = lineStartOf_[begin];
    if (isWord_[begin]) wordStart_[wordStarts_[wordIdx_[begin]]] = {};
    if (isBig_[begin]) bigStart_[bigStarts_[bigIdx_[begin]]] = {};
    if (isWs_[begin]) wsStart_[wsRunStart_[begin]] = {};

    Arrival e;
    for (int ri = begin + 1; ri <= end; ri++) {
      const int last = ri - 1;
      if (isWord_[last]) lower(wordStart_[wordStarts_[wordIdx_[last]]], at(last));
      if (isBig_[last]) lower(bigStart_[bigStarts_[bigIdx_[last]]], at(last));
      if (isWs_[last]) lower(wsStart_[wsRunStart_[last]], at(last));
      if (lineIdx_[ri] >= 0) {
        lineMin_[lineIdx_[ri] - 1] = lineMin;
        lineMin = {};
        lineStart = ri;
      }
      if (paraIdx_[ri] >= 0) {
        paraMin_[paraIdx_[ri] - 1] = paraMin;
        paraMin = {};
      }
      e = {.at = ri};
      for (int k = 1; k <= mxcnt_ && ri - k >= begin; k++)
        relax(e, reach_[ri - k], chunk_[CHAR], k);
      relaxRuns(e, begin, ri, chunk_[WORD], isWord_, wordIdx_, wordStarts_, wordEnds_, wordStart_);
      relaxRuns(e, begin, ri, chunk_[BIG_WORD], isBig_, bigIdx_, bigStarts_, bigEnds_, bigStart_);
      relaxUnits(e, begin, ri, chunk_[LINE], lineIdx_, lineStarts_, lineMin_);
      relaxUnits(e, begin, ri, chunk_[PARAGRAPH], paraIdx_, paraStarts_, paraMin_);
      if (ri == N_ || isNewline_[ri]) relax(e, lineMin, toLineEnd_, 1);  // D / $
      if (kind_ == Kind::Delete && lineStart >= begin && ri > lineStart)  // d0
        relax(e, reach_[lineStart], toLineStart_, 1);
      sink(ri, e);
      reach_[ri] = e;
      lower(reach_[ri], at(ri));
      if (ri < N_) {
        if (isWord_[ri] && wordStarts_[wordIdx_[ri]] == ri) wordStart_[ri] = reach_[ri];
        if (isBig_[ri] && bigStarts_[bigIdx_[ri]] == ri) bigStart_[ri] = reach_[ri];
        if (isWs_[ri] && wsRunStart_[ri] == ri) wsStart_[ri] = reach_[ri];
      }
      lower(lineMin, reach_[ri]);
      lower(paraMin, reach_[ri]);
    }
  }

private:
  struct Chunk {
    ChunkKind kind = CHUNK_CHAR;
    double base = 0.0;
    double gap = 0.0;  // extra cost of splitting one {k}cmd in two
    std::vector<double> pen;
    double cost(int k) const { return base + pen[k]; }
  };

  // Extend `base` by one `{k}cmd` chunk into `acc`'s position.
  static void relax(Arrival& acc, const Arrival& base, const Chunk& chunk, int k) {
    relax(acc, base, chunk.cost(k), chunk.kind, k);
  }
  static void relax(Arrival& acc, const Arrival& base, double add, ChunkKind kind, int k) {
    if (base.cost + add < acc.cost)
      acc = {base.cost + add, base.seed, acc.at, base.at, kind, (int8_t)k};
  }
  // Take the cheaper arrival as is (aggregation over positions).
  static void lower(Arrival& acc, const Arrival& base) {
    if (base.cost < acc.cost) acc = base;
  }

  double maxChunkGap() const;
  int lineOf(int ri) const;
  int paraOf(int ri) const;
  void buildBoundaries(const std::string& text);

  template<CountClass Del, CountClass Mov>
  void buildChunk(Level level, double scale) {
    Chunk& c = chunk_[level];
    c.kind = ChunkKind(level);
    c.base =(kind_ == Kind::Delete ? DELETE_KEYS[level] : MOVE_KEYS[level]) * scale;
    c.pen.assign(mxcnt_ + 1, 0.0);
    for (int k = 2; k <= mxcnt_; k++)
      c.pen[k] = kind_ == Kind::Delete ? countPrefixCost<Del>(k, scale)
                                       : countPrefixCost<Mov>(k, scale);
  }

  // de-shape (ri-1 in a run) or dw-shape (ri-1 trailing ws).
  void relaxRuns(Arrival& e, int begin, int ri, const Chunk& chunk,
                 const std::vector<char>& isClass, const std::vector<int>& idx,
                 const std::vector<int>& starts, const std::vector<int>& ends,
                 const std::vector<Arrival>& runStart) const;
  void relaxUnits(Arrival& e, int begin, int ri, const Chunk& chunk,
                  const std::vector<int>& idxAt, const std::vector<int>& starts,
                  const std::vector<Arrival>& unitMin) const;

  int N_;
  int mxcnt_;  // CostOptions::maxPrefixCount
  Kind kind_;
  std::array<Chunk, LEVEL_COUNT> chunk_;
  Chunk toLineEnd_, toLineStart_;  // uncounted: pen = {0, 0}
  std::vector<char> isWord_, isBig_, isWs_, isNewline_;
  std::vector<int> lineStartOf_;
  std::vector<int> wsRunStart_;
  std::vector<int> wordStarts_, wordEnds_, bigStarts_, bigEnds_;
  std::vector<int> wordIdx_, bigIdx_;
  std::vector<int> lineStarts_, paraStarts_;
  std::vector<int> lineIdx_, paraIdx_;
  // Sweep scratch; run-start slots also hold sources inside the run (span-local rule).
  std::vector<Arrival> reach_, wordStart_, bigStart_, wsStart_, lineMin_, paraMin_;
};

// Typing raw goal [begin,end) costs PS[end] - PS[begin]: per-key effort summed,
// no bigram terms, so the planner's insert side is as coarse as its delete side.
struct Typing {
  std::vector<double> PS;
  double entry, esc;  // the `i` and `<Esc>` keystrokes

  Typing(const FlatText& goal, const Config& config);

  double ins(int begin, int end) const { return PS[end] - PS[begin]; }
};

}  // namespace VimDiff
