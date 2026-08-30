#include "VimDiff.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "MyersDiff.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Utils/Debug.h"
#include "VimCore/CharMask.h"

using namespace std;

namespace VimDiff {
namespace {

constexpr double INF = numeric_limits<double>::max() / 4.0;

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
class TilingCost {
public:
  enum class Kind { Delete, Move };

  TilingCost(const FlatText& initial, double scale, int maxPrefixCount, Kind kind)
      : N_((int)initial.text.size()), cap_(maxPrefixCount), kind_(kind),
        lineStarts_(initial.lineStarts), paraStarts_(initial.paraStarts) {
    buildBoundaries(initial.text);
    buildChunk<CountClass::EditChar, CountClass::MovementChar>(CHAR, scale);
    buildChunk<CountClass::EditWord, CountClass::MovementWord>(WORD, scale);
    buildChunk<CountClass::EditBigWord, CountClass::MovementBigWord>(BIG_WORD, scale);
    buildChunk<CountClass::EditLine, CountClass::MovementLine>(LINE, scale);
    buildChunk<CountClass::EditParagraph, CountClass::MovementParagraph>(PARAGRAPH, scale);
    toLineEnd_ = TO_LINE_END_KEYS * scale;
    toLineStart_ = TO_LINE_START_KEYS * scale;
    scalar_ = makeScratch<double>(INF);
    for (Chunk& c : chunk_) {
      c.gap = c.base;
      for (int s = 2; s <= cap_; s++)
        for (int a = 1; a <= s / 2; a++)
          c.gap = max(c.gap, c.base + c.pen[a] + c.pen[s - a] - c.pen[s]);
    }
  }

  // Upper bound on tiling the segment [x,y) of one line (the trailing newline is
  // allowed) with char or bigword chunks — both always applicable there.
  double coverBound(int x, int y) const {
    const int len = y - x;
    if (len <= 0) return 0.0;
    const double charC = chainCost(CHAR, len);
    const int firstRun =
        (int)(lower_bound(bigEnds_.begin(), bigEnds_.end(), x + 1) - bigEnds_.begin());
    const int endRun =
        (int)(lower_bound(bigStarts_.begin(), bigStarts_.end(), y) - bigStarts_.begin());
    const int runs = max(0, endRun - firstRun);
    const double bigC = chainCost(BIG_WORD, runs) + chunk_[BIG_WORD].cost(1);
    return min(charC, bigC);
  }

  // Upper bound on covering `units` units of one level as a chain of counted
  // commands within the cap.
  double chainCost(Level level, int units) const {
    if (units <= 0) return 0.0;
    return ((units + cap_ - 1) / cap_) * chunk_[level].cost(min(units, cap_));
  }

  // Extra cost any tiling pays to STOP exactly at `edge` instead of crossing it,
  // by what could cross: a counted chunk (its split gap), {k}dd (split gap +
  // cover the edge's partial line), {k}dap (split gap + a counted dd chain to
  // the edge's line + cover), or a D/d0 piece (the cover alone, subsumed).
  double stopSlack(int edge) const {
    const double cover = coverBound(lineStartOf_[edge], edge);
    const int line = lineOf(edge);
    const int paraFirstLine = lineOf(paraStarts_[paraOf(edge)]);
    const int j = line - paraFirstLine;
    double slack = maxChunkGap();
    slack = max(slack, chunk_[LINE].gap + cover);
    slack = max(slack, chunk_[PARAGRAPH].gap + chainCost(LINE, j) + cover);
    return slack;
  }

  // Mirror of stopSlack: extra cost to START exactly at `edge` — cover to the
  // end of edge's line (D/$ + the newline char) before a clean line/paragraph
  // continuation, or a within-line cover for a d0/$ piece crossing `edge`.
  double startSlack(int edge) const {
    const int line = lineOf(edge);
    const int nextStart = lineStarts_[min(line + 1, (int)lineStarts_.size() - 1)];
    const double toEol = toLineEnd_ + chunk_[CHAR].cost(1);
    const int nextPara = paraStarts_[min(paraOf(edge) + 1, (int)paraStarts_.size() - 1)];
    const int jTail = max(0, lineOf(nextPara) - line - 1);
    double slack = maxChunkGap();
    slack = max(slack, coverBound(edge, nextStart));
    slack = max(slack, toEol + chunk_[LINE].gap);
    slack = max(slack, toEol + chunk_[PARAGRAPH].gap + chainCost(LINE, jTail));
    return slack;
  }

  // Cheapest counted-command tiling of raw [begin,end); 0 when begin>=end.
  double query(int begin, int end) {
    double cost = 0.0;
    sweep(begin, end, [&](int ri, double c) { if (ri == end) cost = c; });
    return cost;
  }

  // Single-source scalar pass from raw `begin`: `sink(ri, cost)` receives the
  // cheapest tiling of [begin,ri) for every ri in (begin,end].
  template<class Sink>
  void sweep(int begin, int end, Sink&& sink) {
    static const double ZERO = 0.0;
    sweep(ScalarOps{}, scalar_, begin, end,
          [&](int ri) -> const double& { return ri == begin ? ZERO : INF; },
          [&](int ri, const double& cost) { sink(ri, cost); });
  }

  // Sweep storage for value type V (scalar cost or candidate list), indexed by
  // raw position except lineMin/paraMin (by unit). A run's start slot holds what
  // reached the run start plus every source within the run: the span-local rule
  // that a mid-run source prices the rest of the run as one command.
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

  // Multi-source tiling over raw [begin,end]: `src(ri)` is the value of starting
  // at ri (ops.inf() where nothing starts); `sink(ri, e)` receives, for each ri
  // in (begin,end], the cheapest tiling ending at ri — sources at ri itself
  // excluded, so `e` always covers something. A sweep only reads slots it wrote
  // or reset itself, so nothing is cleared between sweeps.
  template<class Ops, class Src, class Sink>
  void sweep(const Ops& ops, Scratch<typename Ops::V>& s, int begin, int end, Src&& src,
             Sink&& sink) {
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
  // One level's counted command: `{k}cmd` costs base + pen[k], pen 0 at k<=1.
  // `gap` is the extra cost of splitting one {k}cmd in two at a unit boundary: a
  // second base plus the worst count-penalty split gap (digits included in pen).
  struct Chunk {
    double base = 0.0;
    double gap = 0.0;
    vector<double> pen;
    double cost(int k) const { return base + pen[k]; }
  };

  double maxChunkGap() const {
    return max({chunk_[CHAR].gap, chunk_[WORD].gap, chunk_[BIG_WORD].gap});
  }
  int lineOf(int ri) const {
    return (int)(upper_bound(lineStarts_.begin(), lineStarts_.end(), ri) -
                 lineStarts_.begin()) - 1;
  }
  int paraOf(int ri) const {
    return (int)(upper_bound(paraStarts_.begin(), paraStarts_.end(), ri) -
                 paraStarts_.begin()) - 1;
  }

  int N_;
  int cap_;  // shared counted-command cap (CostOptions::maxPrefixCount)
  Kind kind_;
  array<Chunk, LEVEL_COUNT> chunk_;
  double toLineEnd_, toLineStart_;
  vector<char> isWord_, isBig_, isWs_, isNewline_;
  vector<int> lineStartOf_;              // position -> start of its line
  vector<int> wsRunStart_;               // ws position -> start of its whitespace run
  vector<int> wordStarts_, wordEnds_;    // ordered alnum/_ runs
  vector<int> bigStarts_, bigEnds_;      // ordered non-blank runs
  vector<int> wordIdx_, bigIdx_;         // word/big char -> index into the run lists
  vector<int> lineStarts_, paraStarts_;  // from FlatText: ascending, terminated by N_
  vector<int> lineIdx_, paraIdx_;        // line/para start position -> list index, else -1
  Scratch<double> scalar_;               // reused by the scalar sweep

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
    c.pen.assign(cap_ + 1, 0.0);
    for (int k = 2; k <= cap_; k++)
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
    for (int k = 1; k <= cap_ && runIdx - k + 1 >= 0; k++) {
      const int run = runIdx - k + 1;
      if (ends[run] <= begin) break;  // run fully before the start
      const int runBegin = starts[run];
      ops.relax(e, runStart[runBegin], chunk.cost(k));
      if (runBegin > begin && isWs_[runBegin - 1] && k + endsInWs <= cap_)
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

// FNV-1a over a byte range; O(1) unit-match checks.
uint64_t fnv1a(string_view s) {
  uint64_t h = 1469598103934665603ull;
  for (char c : s) {
    h ^= (unsigned char)c;
    h *= 1099511628211ull;
  }
  return h;
}

// ---- Stage 1: collapse matched runs ----------------------------------------

// The DP's coordinate system: `n`/`m` pruned units per side, each a raw span.
// Changed regions and short matched runs stay char-level (one unit per char); the
// interior of a provably-kept matched run is one unit on both sides.
struct PrunedUnits {
  int n = 0, m = 0;
  vector<int> initialRaw, goalRaw;  // pruned index -> raw position; size n+1 / m+1
  vector<int> prunedAt;             // raw initial position -> pruned index, else -1
  vector<uint64_t> initialHash, goalHash;  // per unit
  int prefixUnits = 0, suffixUnits = 0;  // matched units on the leading/trailing diagonal

  // Initial unit i-1 matches goal unit j-1: equal length and content (hashed).
  bool unitMatch(int i, int j) const {
    return initialRaw[i] - initialRaw[i - 1] == goalRaw[j] - goalRaw[j - 1] &&
           initialHash[i - 1] == goalHash[j - 1];
  }
  string_view initialText(const FlatText& initial, int begin, int end) const {
    return string_view(initial.text).substr(initialRaw[begin], initialRaw[end] - initialRaw[begin]);
  }
  string_view goalText(const FlatText& goal, int begin, int end) const {
    return string_view(goal.text).substr(goalRaw[begin], goalRaw[end] - goalRaw[begin]);
  }
};

// The interior of each Myers-matched run the optimum provably keeps becomes one
// unit, with char-level units kept at each edge where an optimal edit boundary
// could still slide into the run. One question decides both, asked of the run's
// core and then of each edge's prefixes: is retyping this shared text ever
// cheaper than the merge bonus of deleting straight through it? The bonus is
// bounded without knowing where the surrounding deletes begin or end — chop the
// through-deletion at the two edges and repair the two cut commands
// (TilingCost::stopSlack / startSlack) — so:
//
//   gate:   collapse the core iff  type(core) > move(core) + (entry+<Esc>) + both edge slacks
//   margin: keep depth d while  ins(d edge chars) <= edge slack + move saving of d
//
// Retype grows ~one keystroke per char (minus at most one bigram seam,
// `seamMax`) while the slack is fixed per edge, so the margin scan crosses
// over quickly; the move saving is priced exactly by a sweep from the edge.
PrunedUnits collapseMatchedRuns(const FlatText& initial, const FlatText& goal,
                                const Lines& initialLines, const Lines& goalLines,
                                const Config& config, const CostOptions& options) {
  PrunedUnits p;
  const int N = (int)initial.text.size();
  const int M = (int)goal.text.size();
  p.initialRaw.push_back(0);
  p.goalRaw.push_back(0);
  auto addInitialUnit = [&](int riEnd) {
    const int riBegin = p.initialRaw.back();
    p.initialHash.push_back(fnv1a(string_view(initial.text).substr(riBegin, riEnd - riBegin)));
    p.initialRaw.push_back(riEnd);
    p.n++;
  };
  auto addGoalUnit = [&](int rjEnd) {
    const int rjBegin = p.goalRaw.back();
    p.goalHash.push_back(fnv1a(string_view(goal.text).substr(rjBegin, rjEnd - rjBegin)));
    p.goalRaw.push_back(rjEnd);
    p.m++;
  };
  auto addCharSpan = [&](int riBegin, int riEnd, int rjBegin, int rjEnd) {
    for (int ri = riBegin + 1; ri <= riEnd; ri++) addInitialUnit(ri);
    for (int rj = rjBegin + 1; rj <= rjEnd; rj++) addGoalUnit(rj);
  };

  TilingCost move(initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Move);
  TilingCost del(initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Delete);
  const double insertOverhead = getEffort("i", config) + getEffort("<Esc>", config);

  auto charEffort = [&](int ri) {
    KeyedSequence one;
    one.append(string_view(initial.text).substr(ri, 1));
    return RunningEffort(one.keys, config);
  };
  // Worst bigram correction a region seam can contribute: retyped run chars
  // decompose as their own effort plus one seam cut against arbitrary adjacent
  // inserted text, bounded over the chars that actually occur.
  double seamMax = 0.0;
  {
    string chars;
    for (const string* text : {&initial.text, &goal.text})
      for (char c : *text)
        if (chars.find(c) == string::npos) chars += c;
    vector<RunningEffort> eff;
    vector<double> effOne(chars.size());
    for (int c = 0; c < (int)chars.size(); c++) {
      KeyedSequence one;
      one.append(string_view(chars).substr(c, 1));
      eff.emplace_back(one.keys, config);
      effOne[c] = eff[c].getEffort(config);
    }
    for (int x = 0; x < (int)chars.size(); x++)
      for (int y = 0; y < (int)chars.size(); y++)
        seamMax = max(seamMax, fabs(RunningEffort::merge(eff[x], eff[y]).getEffort(config) -
                                    effOne[x] - effOne[y]));
  }
  // Deepest d whose retype could still pay: first the delete-merge saving
  // (bounded per edge by stopSlack/startSlack), then the move saving. Left
  // edge: the move saving is at most move(edge, edge+d), exact from one sweep.
  // Right edge: it is at most move(end-d, end), bounded through one sweep at
  // mid = end - dmax plus the cost of stopping that sweep's tiling at end-d.
  // Not monotone in d (a deeper boundary can land on a cheaper structural
  // position), so scan the full half.
  auto leftMargin = [&](int begin, int dmax) {
    if (begin == 0 || dmax <= 0) return 0;  // no region precedes the leading run
    vector<double> mv(dmax + 1, 0.0);
    move.sweep(begin, begin + dmax, [&](int ri, double c) { mv[ri - begin] = c; });
    const double slack = del.stopSlack(begin) + seamMax;
    int margin = 0;
    RunningEffort ins = charEffort(begin);
    for (int d = 1; d <= dmax; d++) {
      if (d > 1) ins.appendFrom(charEffort(begin + d - 1), config);
      if (ins.getEffort(config) <= slack + mv[d]) margin = d;
    }
    return margin;
  };
  auto rightMargin = [&](int end, int dmax) {
    if (end == (int)initial.text.size() || dmax <= 0) return 0;  // no region follows
    vector<double> mv(dmax + 1, 0.0);
    move.sweep(end - dmax, end, [&](int ri, double c) { mv[ri - (end - dmax)] = c; });
    const double slack = del.startSlack(end) + seamMax;
    int margin = 0;
    RunningEffort ins = charEffort(end - 1);
    for (int d = 1; d <= dmax; d++) {
      if (d > 1) ins = RunningEffort::merge(charEffort(end - d), ins);
      const double mvSaving = mv[dmax] - mv[dmax - d] + move.stopSlack(end - d);
      if (ins.getEffort(config) <= slack + mvSaving) margin = d;
    }
    return margin;
  };

  // Matched/changed regions via line-level Myers (gaps = matched runs).
  int initialAt = 0, goalAt = 0;
  auto matchedRun = [&](int riEnd, int rjEnd) {
    const int off = rjEnd - riEnd;
    const int dmax = (riEnd - initialAt) / 2;
    const int coreBegin = initialAt + leftMargin(initialAt, dmax);
    const int coreEnd = riEnd - rightMargin(riEnd, dmax);
    bool collapsed = false;
    if (coreBegin < coreEnd) {
      KeyedSequence typed;
      typed.append(string_view(initial.text).substr(coreBegin, coreEnd - coreBegin));
      const double type = RunningEffort(typed.keys, config).getEffort(config);
      collapsed = type > move.query(coreBegin, coreEnd) + insertOverhead +
                             del.stopSlack(coreBegin) + del.startSlack(coreEnd) + 2 * seamMax;
    }
    if (collapsed) {
      addCharSpan(initialAt, coreBegin, goalAt, coreBegin + off);
      addInitialUnit(coreEnd);
      addGoalUnit(coreEnd + off);
      addCharSpan(coreEnd, riEnd, coreEnd + off, rjEnd);
    } else {
      addCharSpan(initialAt, riEnd, goalAt, rjEnd);
    }
    initialAt = riEnd;
    goalAt = rjEnd;
  };
  for (const DiffState& diff : MyersDiff::calculate(initialLines, goalLines)) {
    const int diffInitialBegin = initial.lineStarts[diff.beginPos.line] + diff.beginPos.col;
    matchedRun(diffInitialBegin, goalAt + (diffInitialBegin - initialAt));
    const int diffInitialEnd = initialAt + (int)diff.deletedText.size();
    const int diffGoalEnd = goalAt + (int)diff.insertedText.size();
    addCharSpan(initialAt, diffInitialEnd, goalAt, diffGoalEnd);
    initialAt = diffInitialEnd;
    goalAt = diffGoalEnd;
  }
  matchedRun(N, M);

  p.prunedAt.assign((int)initial.text.size() + 1, -1);
  for (int i = 0; i <= p.n; i++) p.prunedAt[p.initialRaw[i]] = i;
  while (p.prefixUnits < min(p.n, p.m) && p.unitMatch(p.prefixUnits + 1, p.prefixUnits + 1))
    p.prefixUnits++;
  while (p.suffixUnits < min(p.n, p.m) &&
         p.unitMatch(p.n - p.suffixUnits, p.m - p.suffixUnits))
    p.suffixUnits++;
  return p;
}

// ---- Stage 2: transition costs ---------------------------------------------

// DP edge costs in pruned coordinates. `move[pi][i]` prices crossing the initial
// span [pi,i) as a counted-motion tiling of its raw text (exact across collapsed
// interiors); `typed[j]` is the effort of typing goal unit j and `enter[j]` that
// plus entering insert mode and the closing <Esc>. Deletions have no table: one
// multi-source sweep per column prices them all inside the solve (relaxDeletes).
struct TransitionCosts {
  vector<vector<double>> move;  // [pi][i], pi <= i; 0 on the diagonal
  vector<double> typed, enter;  // per goal unit
};

TransitionCosts calculateTransitionCosts(const FlatText& initial, const FlatText& goal,
                                         const PrunedUnits& p, const Config& config,
                                         const CostOptions& options) {
  TransitionCosts costs;
  costs.move.assign(p.n + 1, vector<double>(p.n + 1, 0.0));
  const int N = (int)initial.text.size();
  const int M = (int)goal.text.size();

  // One sweep per pruned start prices every pruned end at once.
  TilingCost move(initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Move);
  for (int pi = 0; pi < p.n; pi++) {
    move.sweep(p.initialRaw[pi], N, [&](int ri, double cost) {
      if (p.prunedAt[ri] >= 0) costs.move[pi][p.prunedAt[ri]] = cost;
    });
  }

  // RunningEffort is a monoid with one-key boundary context, so typing effort
  // decomposes exactly: PS[rj] is the prefix effort of typing goal[0:rj) and
  // cut[rj] the bigram correction straddling rj; typing raw [begin,end) costs
  // PS[end] - PS[begin] - cut[begin], the cut paid once on entry.
  vector<double> PS(M + 1, 0.0), cut(M + 1, 0.0);
  if (M > 0) {
    vector<RunningEffort> seg;
    vector<double> segEffort(M);
    seg.reserve(M);
    for (int rj = 0; rj < M; rj++) {
      KeyedSequence one;
      one.append(string_view(goal.text).substr(rj, 1));
      seg.emplace_back(one.keys, config);
      segEffort[rj] = seg[rj].getEffort(config);
    }
    RunningEffort acc = seg[0];
    PS[1] = segEffort[0];
    for (int rj = 1; rj < M; rj++) PS[rj + 1] = acc.appendFrom(seg[rj], config);
    for (int rj = 1; rj < M; rj++)
      cut[rj] = RunningEffort::merge(seg[rj - 1], seg[rj]).getEffort(config) -
                segEffort[rj - 1] - segEffort[rj];
  }
  const double insertOverhead = getEffort("i", config) + getEffort("<Esc>", config);
  costs.typed.resize(p.m);
  costs.enter.resize(p.m);
  for (int j = 0; j < p.m; j++) {
    costs.typed[j] = PS[p.goalRaw[j + 1]] - PS[p.goalRaw[j]];
    costs.enter[j] = insertOverhead - cut[p.goalRaw[j]] + costs.typed[j];
  }
  return costs;
}

// ---- Stage 3: the DP -------------------------------------------------------

// Two tables per cell: `out` — normal mode, initial [0,i) consumed and goal
// [0,j) produced — and `in` — insert mode, same coordinates. Nothing is charged
// per region: a region is a maximal stretch of delete/type steps between two
// moves, read off the winning path afterwards, so the state needs no memory of
// where one began.
//
// Each cell keeps its `maxPlans` cheapest candidates keyed by the partition they
// encode so far (XOR of marks at the cells where regions open and close). Equal
// keys are the same plan prefix with identical futures, so only the cheaper is
// kept, and the per-cell top-K is then the global top-K (additive nonnegative
// costs). A candidate names its predecessor by cell and key.
enum Step : int8_t { LEADING, MOVE, DELETE, ENTER, TYPE, EXIT };

struct Cand {
  double cost;
  uint64_t key;   // partition so far
  Step step;      // how the cell was reached
  int pi, pj;     // predecessor cell: `out` for MOVE/DELETE/ENTER, `in` for TYPE/EXIT
  uint64_t pkey;  // predecessor's key within that cell
  bool open() const { return step == DELETE || step == EXIT; }  // `out`: region in progress
};
using Cell = vector<Cand>;  // cost-ascending, one candidate per key

uint64_t mix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}
uint64_t mark(int i, int j, bool close) {
  return mix64(((uint64_t)(uint32_t)i << 33) | ((uint64_t)(uint32_t)j << 1) | (close ? 1 : 0));
}
// Key after starting an edit at (i,j) from `c`, or after ending one there.
uint64_t openKey(const Cand& c, int i, int j) { return c.open() ? c.key : c.key ^ mark(i, j, false); }
uint64_t closeKey(const Cand& c, int i, int j) { return c.open() ? c.key ^ mark(i, j, true) : c.key; }

struct Tables {
  int maxPlans;
  vector<vector<Cell>> out, in;  // [i][j]

  Tables(int n, int m, int maxPlans)
      : maxPlans(maxPlans), out(n + 1, vector<Cell>(m + 1)), in(n + 1, vector<Cell>(m + 1)) {}

  // Insert `from` extended by one step.
  void relax(Cell& cell, const Cand& from, double add, Step step, int pi, int pj,
             uint64_t key) const {
    insert(cell, Cand{from.cost + add, key, step, pi, pj, from.key});
  }

  // Bounded cost-ascending insert; one entry per key (the cheaper wins), equal
  // costs keep generation order. O(maxPlans).
  void insert(Cell& cell, const Cand& c) const {
    for (auto it = cell.begin(); it != cell.end(); ++it) {
      if (it->key != c.key) continue;
      if (it->cost <= c.cost) return;
      cell.erase(it);
      break;
    }
    if ((int)cell.size() == maxPlans && c.cost >= cell.back().cost) return;
    cell.insert(upper_bound(cell.begin(), cell.end(), c,
                            [](const Cand& lhs, const Cand& rhs) { return lhs.cost < rhs.cost; }),
                c);
    if ((int)cell.size() > maxPlans) cell.pop_back();
  }
};

// Every deletion of column j in one multi-source tiling sweep: each `out` cell
// seeds a chunk sequence at its initial position, the sweep extends the cheapest
// in-progress deletion chunk by chunk over raw positions (a deletion's cost does
// not depend on where it started, so all seeds share one carried value), and
// whatever it carries lands on each later pruned cell as a DELETE candidate.
// O(N) per column, independent of n.
void relaxDeletes(Tables& t, const PrunedUnits& p, TilingCost& del,
                  TilingCost::Scratch<Cell>& scratch, vector<Cell>& seeds, int j) {
  struct SweepOps {
    using V = Cell;
    const Tables& t;
    const V& inf() const {
      static const V EMPTY;
      return EMPTY;
    }
    void reset(V& v) const { v.clear(); }
    void relax(V& acc, const V& base, double add) const {
      for (Cand c : base) {
        c.cost += add;
        t.insert(acc, c);
      }
    }
  };
  int begin = -1;
  for (int i = 0; i <= p.n; i++) {
    seeds[i].clear();
    for (const Cand& c : t.out[i][j])
      seeds[i].push_back({c.cost, openKey(c, i, j), DELETE, i, j, c.key});
    if (begin < 0 && !seeds[i].empty()) begin = p.initialRaw[i];
  }
  if (begin < 0) return;
  static const Cell EMPTY;
  del.sweep(
      SweepOps{t}, scratch, begin, (int)p.prunedAt.size() - 1,
      [&](int ri) -> const Cell& { return p.prunedAt[ri] >= 0 ? seeds[p.prunedAt[ri]] : EMPTY; },
      [&](int ri, const Cell& e) {
        if (p.prunedAt[ri] < 0) return;
        for (const Cand& c : e) t.insert(t.out[p.prunedAt[ri]][j], c);
      });
}

// Column-major; within a column `in` depends only on column j-1 and `out` on
// smaller i, so one pass in (j, i) order sees every predecessor complete;
// deletions arrive from the per-column sweep after the cell pass.
Tables solveVimDiff(const PrunedUnits& p, const TransitionCosts& costs, TilingCost& del,
                    int maxPlans) {
  Tables t(p.n, p.m, maxPlans);
  TilingCost::Scratch<Cell> scratch = del.makeScratch<Cell>({});
  vector<Cell> seeds(p.n + 1);
  for (int j = 0; j <= p.m; j++) {
    for (int i = 0; i <= p.n; i++) {
      Cell& in = t.in[i][j];
      Cell& out = t.out[i][j];
      if (j > 0) {
        const int pj = j - 1;
        for (const Cand& c : t.in[i][pj]) t.relax(in, c, costs.typed[pj], TYPE, i, pj, c.key);
        for (const Cand& c : t.out[i][pj])
          t.relax(in, c, costs.enter[pj], ENTER, i, pj, openKey(c, i, pj));
      }
      if (i == j && i <= p.prefixUnits) out.push_back({0.0, 0, LEADING, -1, -1, 0});
      for (int pi = i - 1, pj = j - 1; pi >= 0 && pj >= 0 && p.unitMatch(pi + 1, pj + 1);
           pi--, pj--)
        for (const Cand& c : t.out[pi][pj])
          t.relax(out, c, costs.move[pi][i], MOVE, pi, pj, closeKey(c, pi, pj));
      for (const Cand& c : in) t.relax(out, c, 0.0, EXIT, i, j, c.key);
    }
    relaxDeletes(t, p, del, scratch, seeds, j);
  }
  return t;
}

// ---- Stage 4: plans --------------------------------------------------------

struct Region {  // pruned spans: initial [iBegin,iEnd) -> goal [jBegin,jEnd)
  int iBegin, iEnd, jBegin, jEnd;
  bool operator==(const Region&) const = default;
};

struct PrunedPlan {
  vector<Region> regions;
  double cost = 0.0;
};

const Cand& predecessor(const Tables& t, const Cand& c) {
  const bool predOut = c.step == MOVE || c.step == DELETE || c.step == ENTER;
  for (const Cand& p : predOut ? t.out[c.pi][c.pj] : t.in[c.pi][c.pj])
    if (p.key == c.pkey) return p;
  CHECK(false, "VimDiff: predecessor candidate missing");
  return c;
}

// Regions along the path ending in `out` candidate `c` at (i,j): a region spans
// from the cell it opened at (first delete/type step off a closed candidate) to
// the cell it closed at (the start of the next move, or the end).
vector<Region> walk(const Tables& t, int i, int j, const Cand* c) {
  vector<Region> regions;
  int ci = i, cj = j;  // cell the current region closed at
  while (c->step != LEADING) {
    const Cand& pred = predecessor(t, *c);
    if (c->step == MOVE) {
      if (pred.open()) {
        ci = c->pi;
        cj = c->pj;
      }
    } else if ((c->step == DELETE || c->step == ENTER) && !pred.open()) {
      regions.push_back({c->pi, ci, c->pj, cj});
    }
    c = &pred;
  }
  reverse(regions.begin(), regions.end());
  return regions;
}

// Top-K plans: every `out` candidate on the free trailing diagonal, walked in cost
// order, keeping the `maxPlans` cheapest distinct partitions. An identical-replace
// region (deleted text == inserted text) is strictly dominated by keeping that
// text, so it never appears in the optimum — but the K-best enumeration still
// reaches such partitions, and the transform layer rejects identity edits, so
// they are filtered here.
vector<PrunedPlan> reconstructPlans(const Tables& t, const PrunedUnits& p,
                                    const FlatText& initial, const FlatText& goal) {
  struct Top {
    double cost;
    int i, j;
    const Cand* c;
  };
  vector<Top> tops;
  for (int k = p.suffixUnits; k >= 0; k--)
    for (const Cand& c : t.out[p.n - k][p.m - k])
      if (c.step != LEADING) tops.push_back({c.cost, p.n - k, p.m - k, &c});
  stable_sort(tops.begin(), tops.end(),
              [](const Top& lhs, const Top& rhs) { return lhs.cost < rhs.cost; });

  vector<PrunedPlan> plans;
  for (const Top& top : tops) {
    if ((int)plans.size() == t.maxPlans) break;
    vector<Region> regions = walk(t, top.i, top.j, top.c);
    const bool degenerate = any_of(regions.begin(), regions.end(), [&](const Region& r) {
      return p.initialText(initial, r.iBegin, r.iEnd) == p.goalText(goal, r.jBegin, r.jEnd);
    });
    const bool seen = any_of(plans.begin(), plans.end(),
                             [&](const PrunedPlan& plan) { return plan.regions == regions; });
    if (!degenerate && !seen) plans.push_back({std::move(regions), top.cost});
  }
  return plans;
}

// ---- Pipeline --------------------------------------------------------------

// Hard bound for the dense tables. Future sparse maps should lower n/m before
// this check rather than raising it.
constexpr long long MAX_PLANNER_CELLS = 100'000'000;

struct Planned {
  FlatText initial, goal;
  PrunedUnits pruned;
  TransitionCosts costs;
  vector<PrunedPlan> plans;  // empty when initial already equals goal
};

Planned plan(const Lines& initialLines, const Lines& goalLines, const Config& config,
             const CostOptions& options) {
  Planned p{FlatText(initialLines), FlatText(goalLines)};
  if (p.initial.text == p.goal.text) return p;
  p.pruned = collapseMatchedRuns(p.initial, p.goal, initialLines, goalLines, config, options);
  const long long rows = p.pruned.n + 1, cols = p.pruned.m + 1;
  CHECK(max(rows * cols, rows * rows) <= MAX_PLANNER_CELLS,
        "VimDiff diff too large for the planner DP");
  p.costs = calculateTransitionCosts(p.initial, p.goal, p.pruned, config, options);
  TilingCost del(p.initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Delete);
  const Tables tables = solveVimDiff(p.pruned, p.costs, del, max(1, options.maxPlans));
  p.plans = reconstructPlans(tables, p.pruned, p.initial, p.goal);
  return p;
}

DiffState diffFromRegion(const Lines& initialLines, const Planned& p, const Region& r) {
  string deletedText(p.pruned.initialText(p.initial, r.iBegin, r.iEnd));
  string insertedText(p.pruned.goalText(p.goal, r.jBegin, r.jEnd));
  CursorPos begin = DiffText::flatIndexToPosition(p.pruned.initialRaw[r.iBegin], p.initial.text);
  CursorPos end = DiffText::advancePositionByText(begin, deletedText);
  return DiffState(begin, end, std::move(deletedText), std::move(insertedText),
                   TransformBoundary(initialLines, begin, end));
}

}  // namespace

vector<Plan> calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  const Planned p = plan(initialLines, goalLines, config, options);
  vector<Plan> plans;
  plans.reserve(p.plans.size());
  for (const PrunedPlan& pp : p.plans) {
    Plan result;
    result.cost = pp.cost;
    for (const Region& r : pp.regions) result.diffs.push_back(diffFromRegion(initialLines, p, r));
    plans.push_back(std::move(result));
  }
  return plans;
}

vector<CostBreakdown> calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  const Planned p = plan(initialLines, goalLines, config, options);
  TilingCost delOracle(p.initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Delete);
  vector<CostBreakdown> breakdowns;
  breakdowns.reserve(p.plans.size());
  for (const PrunedPlan& pp : p.plans) {
    CostBreakdown bd;
    int prevEnd = -1;  // pruned initial index of the previous region's end
    for (const Region& r : pp.regions) {
      const double del =
          delOracle.query(p.pruned.initialRaw[r.iBegin], p.pruned.initialRaw[r.iEnd]);
      double ins = 0.0;
      if (r.jBegin < r.jEnd) {
        ins = p.costs.enter[r.jBegin];
        for (int j = r.jBegin + 1; j < r.jEnd; j++) ins += p.costs.typed[j];
      }
      // Inter-region movement: one motion from the previous region's end to this
      // region's begin; the first region is free.
      const double mv = prevEnd < 0 ? 0.0 : p.costs.move[prevEnd][r.iBegin];
      prevEnd = r.iEnd;
      bd.total += del + ins + mv;
      bd.regions.push_back(RegionBreakdown{diffFromRegion(initialLines, p, r), del, ins, mv});
    }
    breakdowns.push_back(std::move(bd));
  }
  return breakdowns;
}

}  // namespace VimDiff
