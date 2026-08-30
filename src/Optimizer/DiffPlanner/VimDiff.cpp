#include "VimDiff.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "MyersDiff.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Utils/Debug.h"
#include "VimCore/CharMask.h"

using namespace std;

// Coordinates: `N`/`M` are the raw initial/goal sizes and `ri`/`rj` raw positions;
// `n`/`m` are the pruned unit counts and `i`/`j` pruned indices (PositionMap maps
// between them). A DP step goes from predecessor cell (pi,pj) to (i,j), or from
// (i,j) to successor (ni,nj).
namespace VimDiff {
namespace {

constexpr double INF = numeric_limits<double>::max() / 4.0;
// Char-level cells kept at each edge of a collapsed matched run, so the DP can slide
// an edit boundary to the cost-optimal alignment (the optimal slide is bounded:
// sliding k chars costs ~k to retype but saves only sub-linear navigation).
constexpr int MATCH_MARGIN = 8;

struct TextRange {
  int begin, end;
  bool operator==(const TextRange&) const = default;
};

struct EditSpan {
  TextRange initial;
  TextRange goal;
  bool operator==(const EditSpan&) const = default;
};

// Flat buffer text plus the unit boundaries the tiling oracle prices line and
// paragraph commands on. Both lists are ascending and terminated by text.size(),
// so unit u is [starts[u], starts[u+1]). A paragraph ends after a blank line.
struct FlatText {
  string text;
  vector<int> lineStarts, paraStarts;

  explicit FlatText(const Lines& lines) : text(lines.flatten()) {
    const int N = (int)text.size();
    bool prevLineBlank = false, lineBlank = true;
    for (int r = 0; r < N; r++) {
      const bool lineStart = r == 0 || text[r - 1] == '\n';
      if (lineStart) lineStarts.push_back(r);
      if (lineStart && (r == 0 || (prevLineBlank && text[r] != '\n'))) paraStarts.push_back(r);
      if (text[r] == '\n') {
        prevLineBlank = lineBlank;
        lineBlank = true;
      } else if (VimCore::CharMask::isBigWord(text[r])) {
        lineBlank = false;
      }
    }
    lineStarts.push_back(N);
    paraStarts.push_back(N);
  }
};

// Chunk levels a span is tiled from, and the keystrokes of each level's uncounted
// command: the bare motion, and the operator form.
enum Level { CHAR, WORD, BIG_WORD, LINE, PARAGRAPH, LEVEL_COUNT };
constexpr double MOVE_KEYS[LEVEL_COUNT] = {1, 1, 2, 1, 2};    // l, w, W, j, }
constexpr double DELETE_KEYS[LEVEL_COUNT] = {1, 2, 3, 2, 3};  // x, dw, dW, dd, dap
// To-boundary commands price a partial line flat, so cutting a line/paragraph
// command mid-line costs a bounded amount: `D`/`$` to the line end, `d0` back to
// the line start.
constexpr double TO_LINE_END_KEYS = 2.0;    // D, $ (shifted)
constexpr double TO_LINE_START_KEYS = 2.0;  // d0

// Cost of a count prefix: the digit keystrokes (scaled like the other keystrokes) plus
// the shared cognitive penalty for the level's class, 0 at k<=1.
template<CountClass C>
double countPrefixCost(int k, double scale) {
  if (k <= 1) return 0.0;
  return (int)to_string(k).size() * scale + runtimeCountPenalty<C>({k, k});
}

// Span cost oracle for deletion/movement over the raw initial text, tiled from
// counted char/word/bigword/line/paragraph commands plus the to-boundary
// commands. Word/bigword chunks are span-local. Deletion chunks `{k}dd`/`{k}dap`
// start on a unit start; movement chunks `{k}j`/`{k}}` start anywhere in their
// first unit (column adjustment on landing is below the oracle's fidelity).
//
// The tiling is one left-to-right `sweep` that is multi-source: any position may
// seed a "start here" value, and a chunk ending at ri relaxes from the value at
// its start. `query` is the single-source scalar instance; the solver seeds it
// with a whole column of K-best lists to price every deletion span at once.
class TilingCost {
public:
  enum class Kind { Delete, Move };

  TilingCost(const FlatText& initial, double scale, Kind kind)
      : N_((int)initial.text.size()), kind_(kind), lineStarts_(initial.lineStarts),
        paraStarts_(initial.paraStarts) {
    buildBoundaries(initial.text);
    buildChunk<CountClass::EditChar, CountClass::MovementChar>(CHAR, scale);
    buildChunk<CountClass::EditWord, CountClass::MovementWord>(WORD, scale);
    buildChunk<CountClass::EditBigWord, CountClass::MovementBigWord>(BIG_WORD, scale);
    buildChunk<CountClass::EditLine, CountClass::MovementLine>(LINE, scale);
    buildChunk<CountClass::EditParagraph, CountClass::MovementParagraph>(PARAGRAPH, scale);
    toLineEnd_ = TO_LINE_END_KEYS * scale;
    toLineStart_ = TO_LINE_START_KEYS * scale;
  }

  // Sweep storage for value type V (scalar cost or candidate list), indexed by
  // raw position except lineMin/paraMin (by unit). A run's start slot holds what
  // reached the run start plus every source within the run: the span-local rule
  // that a source mid-run prices the rest of the run as one command.
  template<class V>
  struct Scratch {
    vector<V> reach, wordStart, bigStart, wsStart, lineMin, paraMin;
  };

  template<class V>
  Scratch<V> makeScratch(const V& inf) const {
    Scratch<V> s;
    for (vector<V>* v : {&s.reach, &s.wordStart, &s.bigStart, &s.wsStart}) v->assign(N_ + 1, inf);
    s.lineMin.assign(lineStarts_.size(), inf);
    s.paraMin.assign(paraStarts_.size(), inf);
    return s;
  }

  struct ScalarOps {
    using V = double;
    const V& inf() const { return INF; }
    void reset(V& v) const { v = INF; }
    void relax(V& acc, const V& base, double add) const { acc = min(acc, base + add); }
  };

  // Cheapest counted-command tiling of the raw span [begin,end), O(span). Queried
  // over raw positions, so the cost is exact across any collapsed-run interior. 0
  // when begin>=end.
  double query(int begin, int end) const {
    if (begin >= end) return 0.0;
    if ((int)scalar_.reach.size() < N_ + 1) scalar_ = makeScratch<double>(INF);
    static const double ZERO = 0.0;
    double cost = INF;
    sweep(ScalarOps{}, scalar_, begin, end,
          [&](int ri) -> const double& { return ri == begin ? ZERO : INF; },
          [&](int ri, const double& e) { if (ri == end) cost = e; });
    return cost;
  }

  // Multi-source tiling over raw [begin,end]: `src(ri)` is the value of starting
  // at ri (ops.inf() where nothing starts); `sink(ri, e)` receives, for each ri in
  // (begin,end], the cheapest tiling ending at ri — sources at ri itself excluded,
  // so `e` always covers something.
  template<class Ops, class Src, class Sink>
  void sweep(const Ops& ops, Scratch<typename Ops::V>& s, int begin, int end, Src&& src,
             Sink&& sink) const {
    using V = typename Ops::V;
    if (begin >= end) return;
    s.reach[begin] = src(begin);
    // Running minima over the current line/paragraph's reached positions: where a
    // from-anywhere command can start. Finalized per unit at the next unit start.
    V lineMin = s.reach[begin], paraMin = s.reach[begin];
    int lineStart = lineStartOf_[begin];
    // Runs cut by `begin` start chunks only at sources >= begin.
    if (isWord_[begin]) ops.reset(s.wordStart[wordStarts_[wordIdx_[begin]]]);
    if (isBig_[begin]) ops.reset(s.bigStart[bigStarts_[bigIdx_[begin]]]);
    if (isWs_[begin]) ops.reset(s.wsStart[wsRunStart_[begin]]);

    V e = ops.inf();
    for (int ri = begin + 1; ri <= end; ri++) {
      const int last = ri - 1;  // last char of any chunk ending at ri
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
      for (int k = 1; k <= CAP && ri - k >= begin; k++)
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
      swap(s.reach[ri], e);
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
  static constexpr int CAP = 9;  // max count scanned for char/word/bigword

  // One level's counted command: `{k}cmd` costs base + pen[k], pen 0 at k<=1.
  struct Chunk {
    double base = 0.0;
    vector<double> pen;
    double cost(int k) const { return base + pen[k]; }
  };

  int N_;
  Kind kind_;
  array<Chunk, LEVEL_COUNT> chunk_;
  double toLineEnd_, toLineStart_;
  mutable Scratch<double> scalar_;  // reused by query()
  vector<char> isWord_, isBig_, isWs_, isNewline_;
  vector<int> lineStartOf_;              // position -> start of its line
  vector<int> wsRunStart_;               // ws position -> start of its whitespace run
  vector<int> wordStarts_, wordEnds_;    // ordered alnum/_ runs
  vector<int> bigStarts_, bigEnds_;      // ordered non-blank runs
  vector<int> wordIdx_, bigIdx_;         // word/big char -> index into the run lists
  vector<int> lineStarts_, paraStarts_;  // from FlatText: ascending, terminated by N_
  vector<int> lineIdx_, paraIdx_;        // line/para start position -> list index, else -1

  void buildBoundaries(const string& text) {
    isWord_.assign(N_, 0);
    isBig_.assign(N_, 0);
    isWs_.assign(N_, 0);
    isNewline_.assign(N_ + 1, 0);
    lineStartOf_.assign(N_ + 1, 0);
    wsRunStart_.assign(N_, 0);
    wordIdx_.assign(N_, -1);
    bigIdx_.assign(N_, -1);
    for (int ri = 0; ri < N_; ri++) {
      isWord_[ri] = VimCore::CharMask::isSmallWord(text[ri]);
      isBig_[ri] = VimCore::CharMask::isBigWord(text[ri]);
      isWs_[ri] = VimCore::CharMask::isWhitespace(text[ri]);
      isNewline_[ri] = text[ri] == '\n';
    }
    for (int ri = 1; ri <= N_; ri++)
      lineStartOf_[ri] = isNewline_[ri - 1] ? ri : lineStartOf_[ri - 1];
    for (int ri = 0; ri < N_; ri++) {
      wsRunStart_[ri] = (isWs_[ri] && ri > 0 && isWs_[ri - 1]) ? wsRunStart_[ri - 1] : ri;
      if (isWord_[ri]) {
        if (ri == 0 || !isWord_[ri - 1]) wordStarts_.push_back(ri);
        wordIdx_[ri] = (int)wordStarts_.size() - 1;
        if (ri + 1 == N_ || !isWord_[ri + 1]) wordEnds_.push_back(ri + 1);
      }
      if (isBig_[ri]) {
        if (ri == 0 || !isBig_[ri - 1]) bigStarts_.push_back(ri);
        bigIdx_[ri] = (int)bigStarts_.size() - 1;
        if (ri + 1 == N_ || !isBig_[ri + 1]) bigEnds_.push_back(ri + 1);
      }
    }
    lineIdx_.assign(N_ + 1, -1);
    paraIdx_.assign(N_ + 1, -1);
    for (int u = 0; u < (int)lineStarts_.size(); u++) lineIdx_[lineStarts_[u]] = u;
    for (int u = 0; u < (int)paraStarts_.size(); u++) paraIdx_[paraStarts_[u]] = u;
  }

  template<CountClass Del, CountClass Mov>
  void buildChunk(Level level, double scale) {
    Chunk& c = chunk_[level];
    c.base = (kind_ == Kind::Delete ? DELETE_KEYS[level] : MOVE_KEYS[level]) * scale;
    c.pen.assign(N_ + 2, 0.0);
    for (int k = 2; k <= N_ + 1; k++)
      c.pen[k] = kind_ == Kind::Delete ? countPrefixCost<Del>(k, scale)
                                       : countPrefixCost<Mov>(k, scale);
  }

  // Counted word/bigword chunks ending at ri: find the run whose chunk ends at ri
  // (ri-1 within/at-end of a run = de/partial, or ri-1 trailing whitespace = dw),
  // then scan k=1..CAP runs back, starting from the run's start slot, or from the
  // whitespace run before it. Starting from that leading space costs one extra
  // count when the chunk also ends in trailing whitespace (`d{k+1}w`), but not in
  // the de-shape (`d{k}e` from the space lands on the k-th run end).
  template<class Ops, class V>
  void relaxRuns(const Ops& ops, V& e, int begin, int ri, const Chunk& chunk,
                 const vector<char>& isClass, const vector<int>& idx, const vector<int>& starts,
                 const vector<int>& ends, const vector<V>& runStart,
                 const vector<V>& wsStart) const {
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
    for (int k = 1; k <= CAP && runIdx - k + 1 >= 0; k++) {
      const int run = runIdx - k + 1;
      if (ends[run] <= begin) break;  // run fully before any source
      const int runBegin = starts[run];
      ops.relax(e, runStart[runBegin], chunk.cost(k));
      if (runBegin > begin && isWs_[runBegin - 1])
        ops.relax(e, wsStart[wsRunStart_[runBegin - 1]], chunk.cost(k + endsInWs));
    }
  }

  // Counted line/paragraph chunks ending at ri (ri must be a unit start or N_), k =
  // number of units between. Deletes start on an earlier reached unit start; moves
  // start anywhere in an earlier unit (its min reach value).
  template<class Ops, class V>
  void relaxUnits(const Ops& ops, V& e, int begin, int ri, const Chunk& chunk,
                  const vector<int>& idxAt, const vector<int>& starts, const vector<V>& reach,
                  const vector<V>& unitMin) const {
    if (idxAt[ri] < 0) return;
    const int unit = idxAt[ri];
    for (int u = unit - 1; u >= 0; u--) {
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

// FNV-1a over a byte range; O(1) unit-match checks.
uint64_t fnv1a(string_view s) {
  uint64_t h = 1469598103934665603ull;
  for (char c : s) {
    h ^= (unsigned char)c;
    h *= 1099511628211ull;
  }
  return h;
}

// The DP's coordinate system: pruned index <-> raw position. `collapse=false` is
// plain char-level (every position a unit) — the exact baseline. `collapse=true`
// skips the interior of matched runs the optimum provably keeps (type(run) >
// move(run) + region overhead), so the pruned unit count is diff-sized. Changed
// regions and short/spannable matched runs stay char-level, so the DP over these
// units still explores every alignment within a changed region and is exact
// there; only provably-kept runs are skipped.
class PositionMap {
public:
  PositionMap(const FlatText& initial, const FlatText& goal, const Lines& initialLines,
              const Lines& goalLines, const Config& config, const CostOptions& options,
              bool collapse) {
    const int N = (int)initial.text.size();
    const int M = (int)goal.text.size();

    if (!collapse) {
      initialPos_.reserve(N + 1);
      goalPos_.reserve(M + 1);
      for (int ri = 0; ri <= N; ri++) initialPos_.push_back(ri);
      for (int rj = 0; rj <= M; rj++) goalPos_.push_back(rj);
      for (int ri = 0; ri < N; ri++)
        initialHash_.push_back(fnv1a(string_view(initial.text).substr(ri, 1)));
      for (int rj = 0; rj < M; rj++)
        goalHash_.push_back(fnv1a(string_view(goal.text).substr(rj, 1)));
      return;
    }

    TilingCost moveOracle(initial, options.moveDeleteScale, TilingCost::Kind::Move);
    // The most a split can add per extra region: another insert entry + <Esc>.
    const double regionOverhead = getEffort("i", config) + getEffort("<Esc>", config);

    // Split into matched/changed regions via line-level Myers (gaps = matched runs).
    struct Region {
      int initialBegin, initialEnd, goalBegin, goalEnd;
      bool matched;
    };
    vector<Region> regions;
    int initialAt = 0, goalAt = 0;
    for (const DiffState& diff : MyersDiff::calculate(initialLines, goalLines)) {
      const int diffInitialBegin = initial.lineStarts[diff.beginPos.line] + diff.beginPos.col;
      const int gapLen = diffInitialBegin - initialAt;
      if (gapLen > 0) regions.push_back({initialAt, diffInitialBegin, goalAt, goalAt + gapLen, true});
      const int diffInitialEnd = diffInitialBegin + (int)diff.deletedText.size();
      const int diffGoalBegin = goalAt + gapLen;
      const int diffGoalEnd = diffGoalBegin + (int)diff.insertedText.size();
      if (diffInitialEnd > diffInitialBegin || diffGoalEnd > diffGoalBegin)
        regions.push_back({diffInitialBegin, diffInitialEnd, diffGoalBegin, diffGoalEnd, false});
      initialAt = diffInitialEnd;
      goalAt = diffGoalEnd;
    }
    if (N - initialAt > 0 || M - goalAt > 0) regions.push_back({initialAt, N, goalAt, M, true});

    initialPos_.push_back(0);
    goalPos_.push_back(0);
    // A unit spans [initialPos_.back(), riEnd); a collapsed run is one such unit (its
    // span hashed whole). The Solver prices it via the raw-span oracle, so no
    // per-unit block cost is stored.
    auto addInitialUnit = [&](int riEnd) {
      initialHash_.push_back(fnv1a(
          string_view(initial.text).substr(initialPos_.back(), riEnd - initialPos_.back())));
      initialPos_.push_back(riEnd);
    };
    auto addGoalUnit = [&](int rjEnd) {
      goalHash_.push_back(
          fnv1a(string_view(goal.text).substr(goalPos_.back(), rjEnd - goalPos_.back())));
      goalPos_.push_back(rjEnd);
    };
    auto addCharSpan = [&](int riBegin, int riEnd, int rjBegin, int rjEnd) {
      for (int ri = riBegin + 1; ri <= riEnd; ri++) addInitialUnit(ri);
      for (int rj = rjBegin + 1; rj <= rjEnd; rj++) addGoalUnit(rj);
    };
    for (const Region& rg : regions) {
      if (!rg.matched) {
        addCharSpan(rg.initialBegin, rg.initialEnd, rg.goalBegin, rg.goalEnd);
        continue;
      }
      // Collapse the run interior, keeping MATCH_MARGIN char-level cells at each edge.
      // An optimal edit boundary can slide a little into a matched run for an
      // alignment saving (sub-linear nav gain) but only a bounded distance — sliding k
      // chars costs ~k to retype, so beyond the margin it is never optimal. Keeping
      // the margin char-level lets the DP find that boundary (Plan 1 stays the true
      // optimum); the raw-span oracle prices commands across the collapsed interior.
      const int off = rg.goalBegin - rg.initialBegin;
      const int coreBegin = rg.initialBegin + MATCH_MARGIN;
      const int coreEnd = rg.initialEnd - MATCH_MARGIN;
      bool collapsed = false;
      if (coreBegin < coreEnd) {
        KeyedSequence typed;
        typed.append(string_view(initial.text).substr(coreBegin, coreEnd - coreBegin));
        const double type = RunningEffort(typed.keys, config).getEffort(config);
        const double mv = moveOracle.query(coreBegin, coreEnd);
        collapsed = type > mv + regionOverhead;
      }
      if (collapsed) {
        addCharSpan(rg.initialBegin, coreBegin, rg.goalBegin, coreBegin + off);
        addInitialUnit(coreEnd);
        addGoalUnit(coreEnd + off);
        addCharSpan(coreEnd, rg.initialEnd, coreEnd + off, rg.goalEnd);
      } else {
        addCharSpan(rg.initialBegin, rg.initialEnd, rg.goalBegin, rg.goalEnd);
      }
    }
  }

  int initialUnits() const { return (int)initialPos_.size() - 1; }
  int goalUnits() const { return (int)goalPos_.size() - 1; }
  int initialRaw(int i) const { return initialPos_[i]; }
  int goalRaw(int j) const { return goalPos_[j]; }
  int initialIndex(int ri) const {
    return (int)(lower_bound(initialPos_.begin(), initialPos_.end(), ri) - initialPos_.begin());
  }
  TextRange initialRange(int begin, int end) const {
    return TextRange{initialRaw(begin), initialRaw(end)};
  }
  TextRange goalRange(int begin, int end) const { return TextRange{goalRaw(begin), goalRaw(end)}; }

  // Initial unit i-1 matches goal unit j-1: equal length and content (hashed).
  bool unitMatch(int i, int j) const {
    return initialRaw(i) - initialRaw(i - 1) == goalRaw(j) - goalRaw(j - 1) &&
           initialHash_[i - 1] == goalHash_[j - 1];
  }

private:
  vector<int> initialPos_, goalPos_;  // pruned index -> raw position
  vector<uint64_t> initialHash_, goalHash_;
};

struct PlanSpans {
  vector<EditSpan> spans;
  double cost = 0.0;
};

uint64_t mix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

// K-best DP solver over two tables per cell: `out` — normal mode on the initial
// text, where a step crosses a matched run or deletes a counted chunk — and `in`
// — insert mode, where the only step is typing the next goal unit. Entering
// insert pays `i` + <Esc>; leaving is free. Nothing is charged per region: a
// region is just a maximal stretch of delete/type steps between two moves, read
// off the winning path afterwards.
//
// Each cell keeps its `maxPlans` cheapest candidates keyed by the partition they
// encode so far (a hash of region open/close cells). Equal keys are the same plan
// prefix, so only the cheaper is kept, and the per-cell top-K is then the global
// top-K (additive nonnegative costs). A candidate names its predecessor by cell
// and key, so plans are reconstructed by walking back.
struct Solver {
  enum Step : int8_t { LEADING, MOVE, DELETE, ENTER, TYPE, EXIT };

  struct Cand {
    double cost;
    uint64_t key;   // partition so far
    Step step;      // how the cell was reached
    int pi, pj;     // predecessor cell: `out` for MOVE/DELETE/ENTER, `in` for TYPE/EXIT
    uint64_t pkey;  // predecessor's key within that cell's list
    bool open() const { return step == DELETE || step == EXIT; }  // `out`: region in progress
  };

  const PositionMap& pos;
  const Config& config;
  CostOptions options;
  string_view initial;
  string_view goal;
  int N, M;  // raw sizes
  int n, m;  // pruned unit counts; pruned indices run 0..n and 0..m
  int maxPlans;
  int commonPrefix_ = 0, commonSuffix_ = 0;  // raw leading/trailing matched lengths

  vector<vector<vector<Cand>>> out_, in_;  // [i][j] -> up to maxPlans candidates
  vector<vector<Cand>> src_;               // per initial unit: the column's `out` list as deletion sources
  vector<int> prunedAt_;                   // raw initial position -> pruned index, else -1
  // RunningEffort is a monoid with one-key boundary context, so typing effort
  // decomposes exactly: PS_[rj] is the prefix effort of typing goal[0:rj) and
  // cut_[rj] the bigram correction straddling rj, both over RAW goal positions.
  // Typing raw [begin,end) costs PS_[end] - PS_[begin] - cut_[begin].
  vector<double> PS_, cut_;
  double escEffort_, insertEntryEffort_;
  TilingCost del;   // counted-tiling delete oracle (dd/de/dw/...) over raw initial positions
  TilingCost move;  // same tiling with bare-motion bases (dw->w, dd->j, x->l)
  TilingCost::Scratch<vector<Cand>> sweep_;  // storage for the K-best deletion sweep
  mutable unordered_map<long long, double> moveMemo_, delMemo_;

  Solver(const FlatText& initialFlat, const FlatText& goalFlat, const PositionMap& positions,
         const Config& cfg, CostOptions opts)
      : pos(positions), config(cfg), options(opts),
        initial(initialFlat.text), goal(goalFlat.text),
        N((int)initial.size()), M((int)goal.size()),
        n(positions.initialUnits()), m(positions.goalUnits()), maxPlans(max(1, opts.maxPlans)),
        out_(n + 1, vector<vector<Cand>>(m + 1)),
        in_(n + 1, vector<vector<Cand>>(m + 1)),
        src_(n + 1), prunedAt_(N + 1, -1),
        escEffort_(getEffort("<Esc>", cfg)), insertEntryEffort_(getEffort("i", cfg)),
        del(initialFlat, opts.moveDeleteScale, TilingCost::Kind::Delete),
        move(initialFlat, opts.moveDeleteScale, TilingCost::Kind::Move),
        sweep_(del.makeScratch<vector<Cand>>({})) {
    buildInsPrefix();
    for (int i = 0; i <= n; i++) prunedAt_[pos.initialRaw(i)] = i;
    while (commonPrefix_ < N && commonPrefix_ < M && initial[commonPrefix_] == goal[commonPrefix_])
      commonPrefix_++;
    while (commonSuffix_ < N && commonSuffix_ < M &&
           initial[N - 1 - commonSuffix_] == goal[M - 1 - commonSuffix_])
      commonSuffix_++;
  }

  void buildInsPrefix() {
    PS_.assign(M + 1, 0.0);
    cut_.assign(M + 1, 0.0);
    if (M == 0) return;
    vector<RunningEffort> seg;
    vector<double> segEffort(M);
    seg.reserve(M);
    for (int rj = 0; rj < M; rj++) {
      KeyedSequence one;
      one.append(goal.substr(rj, 1));
      seg.emplace_back(one.keys, config);
      segEffort[rj] = seg[rj].getEffort(config);
    }
    RunningEffort acc = seg[0];
    PS_[1] = segEffort[0];
    for (int rj = 1; rj < M; rj++) PS_[rj + 1] = acc.appendFrom(seg[rj], config);
    for (int rj = 1; rj < M; rj++) {
      cut_[rj] = RunningEffort::merge(seg[rj - 1], seg[rj]).getEffort(config) -
                 segEffort[rj - 1] - segEffort[rj];
    }
  }

  // Single-span oracle over pruned initial indices, priced by tiling the RAW span
  // [initialRaw(begin), initialRaw(end)) — exact across any collapsed run interior.
  // Memoized. Movement uses it per matched run; deletion goes through sweepDeletes.
  double moveCost(int begin, int end) const { return rawCost(move, moveMemo_, begin, end); }
  double delCost(int begin, int end) const { return rawCost(del, delMemo_, begin, end); }
  double rawCost(const TilingCost& oracle, unordered_map<long long, double>& memo, int begin,
                 int end) const {
    const int riBegin = pos.initialRaw(begin), riEnd = pos.initialRaw(end);
    if (riBegin >= riEnd) return 0.0;
    const long long key = (long long)riBegin * (N + 1) + riEnd;
    auto it = memo.find(key);
    if (it != memo.end()) return it->second;
    return memo[key] = oracle.query(riBegin, riEnd);
  }

  // Region open/close mark for the partition key.
  static uint64_t mark(int i, int j, bool close) {
    return mix64(((uint64_t)(uint32_t)i << 33) | ((uint64_t)(uint32_t)j << 1) | (close ? 1 : 0));
  }

  // Bounded cost-ascending insert, one entry per key (the cheaper wins); equal
  // costs keep generation order. O(maxPlans).
  void insert(vector<Cand>& cands, Cand c) const {
    for (auto it = cands.begin(); it != cands.end(); ++it) {
      if (it->key != c.key) continue;
      if (it->cost <= c.cost) return;
      cands.erase(it);
      break;
    }
    if ((int)cands.size() == maxPlans && c.cost >= cands.back().cost) return;
    auto it = upper_bound(cands.begin(), cands.end(), c,
                          [](const Cand& lhs, const Cand& rhs) { return lhs.cost < rhs.cost; });
    cands.insert(it, c);
    if ((int)cands.size() > maxPlans) cands.pop_back();
  }

  struct SweepOps {
    using V = vector<Cand>;
    const Solver& solver;
    const V& inf() const {
      static const V EMPTY;
      return EMPTY;
    }
    void reset(V& v) const { v.clear(); }
    void relax(V& acc, const V& base, double add) const {
      for (Cand c : base) {
        c.cost += add;
        solver.insert(acc, c);
      }
    }
  };

  // Column j depends only on columns < j (`in` typed goal unit j-1; `out` moved
  // over a matched run) and on itself along the initial axis (deletion sweep,
  // `in` -> `out` exit).
  void fillColumn(int j) {
    if (j >= 1) {
      const int pj = j - 1;
      const double typed = PS_[pos.goalRaw(j)] - PS_[pos.goalRaw(pj)];
      const double enter = insertEntryEffort_ + escEffort_ - cut_[pos.goalRaw(pj)] + typed;
      for (int i = 0; i <= n; i++) {
        vector<Cand>& in = in_[i][j];
        for (const Cand& c : in_[i][pj]) insert(in, {c.cost + typed, c.key, TYPE, i, pj, c.key});
        for (const Cand& c : out_[i][pj])
          insert(in, {c.cost + enter, c.open() ? c.key : c.key ^ mark(i, pj, false), ENTER, i, pj,
                      c.key});
      }
    }
    for (int i = 0; i <= n; i++) {
      vector<Cand>& out = out_[i][j];
      if (pos.initialRaw(i) == pos.goalRaw(j) && pos.initialRaw(i) <= commonPrefix_)
        out.push_back({0.0, 0, LEADING, -1, -1, 0});  // leading run: free
      for (int pi = i - 1, pj = j - 1; pi >= 0 && pj >= 0 && pos.unitMatch(pi + 1, pj + 1);
           pi--, pj--) {
        const double mv = moveCost(pi, i);
        for (const Cand& c : out_[pi][pj])
          insert(out, {c.cost + mv, c.open() ? c.key ^ mark(pi, pj, true) : c.key, MOVE, pi, pj,
                       c.key});
      }
      for (const Cand& c : in_[i][j]) insert(out, {c.cost, c.key, EXIT, i, j, c.key});
    }
    sweepDeletes(j);
  }

  // Every deletion of the column in one multi-source tiling sweep: each `out`
  // candidate seeds a chunk sequence at its initial position, and a sequence
  // reaching a later pruned position lands there as a DELETE candidate.
  void sweepDeletes(int j) {
    int riBegin = -1;
    for (int i = 0; i <= n; i++) {
      src_[i].clear();
      for (const Cand& c : out_[i][j])
        src_[i].push_back({c.cost, c.open() ? c.key : c.key ^ mark(i, j, false), DELETE, i, j, c.key});
      if (riBegin < 0 && !src_[i].empty()) riBegin = pos.initialRaw(i);
    }
    if (riBegin < 0) return;
    static const vector<Cand> EMPTY;
    del.sweep(
        SweepOps{*this}, sweep_, riBegin, N,
        [&](int ri) -> const vector<Cand>& {
          return prunedAt_[ri] >= 0 ? src_[prunedAt_[ri]] : EMPTY;
        },
        [&](int ri, const vector<Cand>& e) {
          if (prunedAt_[ri] < 0) return;
          for (const Cand& c : e) insert(out_[prunedAt_[ri]][j], c);
        });
  }

  const Cand& find(const vector<Cand>& cands, uint64_t key) const {
    for (const Cand& c : cands)
      if (c.key == key) return c;
    CHECK(false, "VimDiff: predecessor candidate missing");
    return cands.front();
  }

  // Regions along the path ending in `out` candidate `c` at (i,j): a region spans
  // from the cell it opened at (first delete/type step off a closed candidate)
  // to the cell it closed at (the start of the next move, or the end).
  vector<EditSpan> walk(int i, int j, const Cand* c) const {
    vector<EditSpan> spans;
    int ci = i, cj = j;  // cell the current region closed at
    while (c->step != LEADING) {
      const bool predOut = c->step == MOVE || c->step == DELETE || c->step == ENTER;
      const Cand& pred = find(predOut ? out_[c->pi][c->pj] : in_[c->pi][c->pj], c->pkey);
      if (c->step == MOVE) {
        if (pred.open()) {
          ci = c->pi;
          cj = c->pj;
        }
      } else if (predOut && !pred.open()) {
        spans.push_back(EditSpan{pos.initialRange(c->pi, ci), pos.goalRange(c->pj, cj)});
      }
      c = &pred;
    }
    reverse(spans.begin(), spans.end());
    return spans;
  }

  // An identical-replace region (deleted text == inserted text) is strictly
  // dominated by keeping that text, so it never appears in the optimum — but the
  // K-best enumeration still reaches such partitions, and the transform layer
  // rejects identity edits, so filter them here.
  bool isDegenerate(const vector<EditSpan>& spans) const {
    for (const EditSpan& s : spans) {
      if (initial.substr(s.initial.begin, s.initial.end - s.initial.begin) ==
          goal.substr(s.goal.begin, s.goal.end - s.goal.begin))
        return true;
    }
    return false;
  }

  // Top-K plans overall: every `out` candidate whose trailing run reaches the end
  // (free), walked in cost order, keeping the `maxPlans` cheapest distinct
  // non-degenerate partitions. (A candidate that closed a region and then moved
  // into the trailing run is the same partition as the one that stopped; the walk
  // makes them equal.)
  vector<PlanSpans> reconstructPlans() {
    for (int j = 0; j <= m; j++) fillColumn(j);
    struct Top {
      double cost;
      int i, j;
      const Cand* c;
    };
    vector<Top> tops;
    for (int i = 0; i <= n; i++)
      for (int j = 0; j <= m; j++)
        if (N - pos.initialRaw(i) == M - pos.goalRaw(j) && N - pos.initialRaw(i) <= commonSuffix_)
          for (const Cand& c : out_[i][j])
            if (c.step != LEADING) tops.push_back({c.cost, i, j, &c});
    stable_sort(tops.begin(), tops.end(),
                [](const Top& lhs, const Top& rhs) { return lhs.cost < rhs.cost; });

    vector<PlanSpans> plans;
    plans.reserve(maxPlans);
    for (const Top& top : tops) {
      if ((int)plans.size() == maxPlans) break;
      vector<EditSpan> spans = walk(top.i, top.j, top.c);
      if (isDegenerate(spans)) continue;
      if (any_of(plans.begin(), plans.end(),
                 [&](const PlanSpans& plan) { return plan.spans == spans; }))
        continue;
      plans.push_back({std::move(spans), top.cost});
    }
    return plans;
  }
};

// Hard bound for the dense tables. Future sparse maps should lower n/m before
// this check rather than raising it.
constexpr long long MAX_PLANNER_CELLS = 100'000'000;

void checkPlannerSize(int n, int m) {
  const long long rows = (long long)n + 1;
  const long long cols = (long long)m + 1;
  CHECK(max(rows * cols, rows * rows) <= MAX_PLANNER_CELLS,
        "VimDiff diff too large for the planner DP");
}

DiffState diffFromSpan(const Lines& initialLines, const string& initialText,
                       const string& goalText, const EditSpan& span) {
  string deletedText = initialText.substr(span.initial.begin, span.initial.end - span.initial.begin);
  string insertedText = goalText.substr(span.goal.begin, span.goal.end - span.goal.begin);

  CursorPos begin = DiffText::flatIndexToPosition(span.initial.begin, initialText);
  CursorPos end = DiffText::advancePositionByText(begin, deletedText);

  return DiffState(begin, end, std::move(deletedText), std::move(insertedText),
                   TransformBoundary(initialLines, begin, end));
}

// Shared pipeline for calculate/calculateBreakdown. Members are heap-owned so the
// solver's views/references into them stay valid when the struct is returned.
struct SolvedPipeline {
  std::unique_ptr<FlatText> initial, goal;
  std::unique_ptr<PositionMap> positions;
  std::unique_ptr<Solver> solver;
  vector<PlanSpans> planSpans;
  bool equal = false;  // initial already equals goal
};

SolvedPipeline runPipeline(const Lines& initialLines, const Lines& goalLines,
                           const Config& config, const CostOptions& options,
                           bool needBreakdown) {
  SolvedPipeline pipeline;
  pipeline.initial = std::make_unique<FlatText>(initialLines);
  pipeline.goal = std::make_unique<FlatText>(goalLines);
  if (pipeline.initial->text == pipeline.goal->text) {
    pipeline.equal = true;
    return pipeline;
  }
  // Production collapses matched-run interiors (diff-bound); the breakdown/K-best
  // diagnostic path keeps exact char-level coordinates (small inputs).
  const bool collapse = !needBreakdown && options.collapseRuns;
  pipeline.positions = std::make_unique<PositionMap>(*pipeline.initial, *pipeline.goal,
                                                     initialLines, goalLines, config, options,
                                                     collapse);
  checkPlannerSize(pipeline.positions->initialUnits(), pipeline.positions->goalUnits());
  pipeline.solver = std::make_unique<Solver>(*pipeline.initial, *pipeline.goal,
                                             *pipeline.positions, config, options);
  pipeline.planSpans = pipeline.solver->reconstructPlans();
  return pipeline;
}

}  // namespace

vector<Plan> calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  const SolvedPipeline pipeline = runPipeline(initialLines, goalLines, config, options, false);
  if (pipeline.equal) return {};

  vector<Plan> plans;
  plans.reserve(pipeline.planSpans.size());
  for (const PlanSpans& planSpans : pipeline.planSpans) {
    Plan plan;
    plan.cost = planSpans.cost;
    plan.diffs.reserve(planSpans.spans.size());
    for (const EditSpan& span : planSpans.spans)
      plan.diffs.push_back(
          diffFromSpan(initialLines, pipeline.initial->text, pipeline.goal->text, span));
    plans.push_back(std::move(plan));
  }
  return plans;
}

vector<CostBreakdown> calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  const SolvedPipeline pipeline = runPipeline(initialLines, goalLines, config, options, true);
  if (pipeline.equal) return {};
  const PositionMap& pos = *pipeline.positions;
  const Solver& solver = *pipeline.solver;

  vector<CostBreakdown> breakdowns;
  breakdowns.reserve(pipeline.planSpans.size());
  for (const PlanSpans& planSpans : pipeline.planSpans) {
    CostBreakdown bd;
    double listed = 0.0;
    int prevEnd = -1;  // pruned initial index of the previous region's end
    for (const EditSpan& span : planSpans.spans) {
      DiffState diff =
          diffFromSpan(initialLines, pipeline.initial->text, pipeline.goal->text, span);
      const int iBegin = pos.initialIndex(span.initial.begin);
      const int iEnd = pos.initialIndex(span.initial.end);
      const double del = solver.delCost(iBegin, iEnd);
      KeyedSequence typed;
      typed.append(string_view(pipeline.goal->text)
                       .substr(span.goal.begin, span.goal.end - span.goal.begin));
      double ins = RunningEffort(typed.keys, config).getEffort(config);
      if (span.goal.end > span.goal.begin) ins += solver.insertEntryEffort_ + solver.escEffort_;
      // Inter-region movement: one motion from the previous region's end to this
      // region's begin (one unified DP, so no severing and no coupling); the first
      // region is free.
      const double mv = prevEnd < 0 ? 0.0 : solver.moveCost(prevEnd, iBegin);
      prevEnd = iEnd;
      listed += del + ins + mv;
      bd.regions.push_back(RegionBreakdown{std::move(diff), del, ins, mv});
    }
    bd.total = listed;
    breakdowns.push_back(std::move(bd));
  }
  return breakdowns;
}

}  // namespace VimDiff
