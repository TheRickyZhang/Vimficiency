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

// Unit u is [starts[u], starts[u+1]); both lists end with N.
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

enum Level { CHAR, WORD, BIG_WORD, LINE, PARAGRAPH, LEVEL_COUNT };
constexpr double MOVE_KEYS[LEVEL_COUNT] = {1, 1, 2, 1, 2};    // l, w, W, j, }
constexpr double DELETE_KEYS[LEVEL_COUNT] = {1, 2, 3, 2, 3};  // x, dw, dW, dd, dap
constexpr double TO_LINE_END_KEYS = 2.0;                      // D, $
constexpr double TO_LINE_START_KEYS = 2.0;                    // d0

template<CountClass C>
double countPrefixCost(int k, double scale) {
  if (k <= 1) return 0.0;
  return (int)to_string(k).size() * scale + runtimeCountPenalty<C>({k, k});
}

// Cheapest counted-command tiling of raw initial spans.
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

  // Upper bound on tiling one line's segment [x,y).
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

  double chainCost(Level level, int units) const {
    if (units <= 0) return 0.0;
    return ((units + cap_ - 1) / cap_) * chunk_[level].cost(min(units, cap_));
  }

  // Worst extra cost to end a tiling at `edge` instead of crossing it.
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

  double query(int begin, int end) {
    double cost = 0.0;
    sweep(begin, end, [&](int ri, double c) { if (ri == end) cost = c; });
    return cost;
  }

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
  struct Chunk {
    double base = 0.0;
    double gap = 0.0;  // extra cost of splitting one {k}cmd in two
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
  int cap_;  // CostOptions::maxPrefixCount
  Kind kind_;
  array<Chunk, LEVEL_COUNT> chunk_;
  double toLineEnd_, toLineStart_;
  vector<char> isWord_, isBig_, isWs_, isNewline_;
  vector<int> lineStartOf_;
  vector<int> wsRunStart_;
  vector<int> wordStarts_, wordEnds_, bigStarts_, bigEnds_;
  vector<int> wordIdx_, bigIdx_;
  vector<int> lineStarts_, paraStarts_;
  vector<int> lineIdx_, paraIdx_;
  Scratch<double> scalar_;

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

  // de-shape (ri-1 in a run) or dw-shape (ri-1 trailing ws).
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
      if (ends[run] <= begin) break;
      const int runBegin = starts[run];
      ops.relax(e, runStart[runBegin], chunk.cost(k));
      if (runBegin > begin && isWs_[runBegin - 1] && k + endsInWs <= cap_)
        ops.relax(e, wsStart[wsRunStart_[runBegin - 1]], chunk.cost(k + endsInWs));
    }
  }

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

// Typing raw goal [begin,end) costs PS[end] - PS[begin] - cut[begin].
struct Typing {
  vector<double> PS, cut;
  double insertOverhead;

  Typing(const FlatText& goal, const Config& config)
      : PS(goal.text.size() + 1, 0.0), cut(goal.text.size() + 1, 0.0),
        insertOverhead(getEffort("i", config) + getEffort("<Esc>", config)) {
    const int M = (int)goal.text.size();
    if (M == 0) return;
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

  double ins(int begin, int end) const { return PS[end] - PS[begin] - cut[begin]; }
};

// ---- Stage 1: seal matched runs --------------------------------------------

// Raw spans of one alignment block. Consecutive blocks are separated by a sealed
// run that no optimal plan edits into, so deletions never cross it.
struct Block {
  int aBegin, aEnd, bBegin, bEnd;
  int n() const { return aEnd - aBegin; }
  int m() const { return bEnd - bBegin; }
};

//   gate:   type(core) > move(core) + entry+<Esc> + stopSlack + startSlack
//   margin: keep depth d while ins(d) <= edge slack + move saving(d)
vector<Block> sealMatchedRuns(const FlatText& initial, const FlatText& goal, const Typing& typing,
                              const Lines& initialLines, const Lines& goalLines,
                              const Config& config, const CostOptions& options) {
  const int N = (int)initial.text.size();
  const int M = (int)goal.text.size();
  TilingCost move(initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Move);
  TilingCost del(initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Delete);

  double seamMax = 0.0;  // largest bigram correction at a retype seam
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
  // Run chars are matched, so their retype cost is read off the goal prefix sums.
  // Not monotone in d, so the whole half is scanned.
  auto leftMargin = [&](int begin, int off, int dmax) {
    if (begin == 0 || dmax <= 0) return 0;
    vector<double> mv(dmax + 1, 0.0);
    move.sweep(begin, begin + dmax, [&](int ri, double c) { mv[ri - begin] = c; });
    const double slack = del.stopSlack(begin) + seamMax;
    int margin = 0;
    for (int d = 1; d <= dmax; d++)
      if (typing.ins(begin + off, begin + off + d) <= slack + mv[d]) margin = d;
    return margin;
  };
  auto rightMargin = [&](int end, int off, int dmax) {
    if (end == N || dmax <= 0) return 0;
    vector<double> mv(dmax + 1, 0.0);
    move.sweep(end - dmax, end, [&](int ri, double c) { mv[ri - (end - dmax)] = c; });
    const double slack = del.startSlack(end) + seamMax;
    int margin = 0;
    for (int d = 1; d <= dmax; d++) {
      const double mvSaving = mv[dmax] - mv[dmax - d] + move.stopSlack(end - d);
      if (typing.ins(end + off - d, end + off) <= slack + mvSaving) margin = d;
    }
    return margin;
  };

  vector<Block> blocks;
  Block cur{0, 0, 0, 0};
  int initialAt = 0, goalAt = 0;
  auto matchedRun = [&](int riEnd, int rjEnd) {
    const int off = rjEnd - riEnd;
    const int dmax = (riEnd - initialAt) / 2;
    const int coreBegin = initialAt + leftMargin(initialAt, off, dmax);
    const int coreEnd = riEnd - rightMargin(riEnd, off, dmax);
    bool sealed = false;
    if (coreBegin < coreEnd) {
      sealed = typing.ins(coreBegin + off, coreEnd + off) >
               move.query(coreBegin, coreEnd) + typing.insertOverhead +
                   del.stopSlack(coreBegin) + del.startSlack(coreEnd) + 2 * seamMax;
    }
    if (sealed) {
      cur.aEnd = coreBegin;
      cur.bEnd = coreBegin + off;
      if (cur.n() > 0 || cur.m() > 0) blocks.push_back(cur);
      cur = Block{coreEnd, coreEnd, coreEnd + off, coreEnd + off};
    }
    initialAt = riEnd;
    goalAt = rjEnd;
  };
  for (const DiffState& diff : MyersDiff::calculate(initialLines, goalLines)) {
    const int diffInitialBegin = initial.lineStarts[diff.beginPos.line] + diff.beginPos.col;
    matchedRun(diffInitialBegin, goalAt + (diffInitialBegin - initialAt));
    initialAt += (int)diff.deletedText.size();
    goalAt += (int)diff.insertedText.size();
  }
  matchedRun(N, M);
  cur.aEnd = N;
  cur.bEnd = M;
  if (cur.n() > 0 || cur.m() > 0) blocks.push_back(cur);
  return blocks;
}

// ---- Stage 2: transition costs ---------------------------------------------

struct BlockCosts {
  int lead = 0, trail = 0;       // matched diagonal length at the block's start / end
  vector<vector<double>> move;   // [pi][i]
  vector<vector<double>> cross;  // [t][i]: from the previous block's (n-t, m-t) to (i,i)
  vector<double> typed, enter;   // per goal unit
};

vector<BlockCosts> calculateTransitionCosts(const FlatText& initial, const FlatText& goal,
                                            const Typing& typing, const vector<Block>& blocks,
                                            const CostOptions& options) {
  TilingCost move(initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Move);
  auto matches = [&](int ra, int rb) { return initial.text[ra] == goal.text[rb]; };

  vector<BlockCosts> all(blocks.size());
  for (int k = 0; k < (int)blocks.size(); k++) {
    const Block& b = blocks[k];
    BlockCosts& c = all[k];
    const int n = b.n(), m = b.m();
    while (c.lead < min(n, m) && matches(b.aBegin + c.lead, b.bBegin + c.lead)) c.lead++;
    while (c.trail < min(n, m) && matches(b.aEnd - 1 - c.trail, b.bEnd - 1 - c.trail)) c.trail++;

    c.move.assign(n + 1, vector<double>(n + 1, 0.0));
    for (int pi = 0; pi < n; pi++)
      move.sweep(b.aBegin + pi, b.aEnd,
                 [&](int ri, double cost) { c.move[pi][ri - b.aBegin] = cost; });

    if (k > 0) {
      const Block& prev = blocks[k - 1];
      const int trail = all[k - 1].trail;
      c.cross.assign(trail + 1, vector<double>(c.lead + 1, 0.0));
      for (int t = 0; t <= trail; t++)
        move.sweep(prev.aEnd - t, b.aBegin + c.lead, [&](int ri, double cost) {
          if (ri >= b.aBegin) c.cross[t][ri - b.aBegin] = cost;
        });
    }

    c.typed.resize(m);
    c.enter.resize(m);
    for (int j = 0; j < m; j++) {
      const int rj = b.bBegin + j;
      c.typed[j] = typing.PS[rj + 1] - typing.PS[rj];
      c.enter[j] = typing.insertOverhead - typing.cut[rj] + c.typed[j];
    }
  }
  return all;
}

// ---- Stage 3: the DP -------------------------------------------------------

// out[i][j]: normal mode, initial [0,i) consumed, goal [0,j) produced; in[i][j]: insert mode.
// A cell keeps `maxPlans` candidates, one per partition key (XOR of region open/close marks).
enum Step : int8_t { LEADING, MOVE, CROSS, DELETE, ENTER, TYPE, EXIT };

struct Cand {
  double cost;
  uint64_t key;
  Step step;
  int pi, pj;     // predecessor cell; in the previous block for CROSS
  uint64_t pkey;  // predecessor's key within that cell
  bool open() const { return step == DELETE || step == EXIT; }  // region in progress
};
using Cell = vector<Cand>;

uint64_t mix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}
uint64_t mark(int ra, int rb, bool close) {
  return mix64(((uint64_t)(uint32_t)ra << 33) | ((uint64_t)(uint32_t)rb << 1) | (close ? 1 : 0));
}
uint64_t openKey(const Cand& c, int ra, int rb) { return c.open() ? c.key : c.key ^ mark(ra, rb, false); }
uint64_t closeKey(const Cand& c, int ra, int rb) { return c.open() ? c.key ^ mark(ra, rb, true) : c.key; }

struct Tables {
  int maxPlans;
  vector<vector<Cell>> out, in;

  Tables(int n, int m, int maxPlans)
      : maxPlans(maxPlans), out(n + 1, vector<Cell>(m + 1)), in(n + 1, vector<Cell>(m + 1)) {}

  void relax(Cell& cell, const Cand& from, double add, Step step, int pi, int pj,
             uint64_t key) const {
    insert(cell, Cand{from.cost + add, key, step, pi, pj, from.key});
  }

  // Cost-ascending, one entry per key.
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

// One multi-source sweep prices every deletion of column j within the block.
void relaxDeletes(Tables& t, const Block& b, TilingCost& del, TilingCost::Scratch<Cell>& scratch,
                  vector<Cell>& seeds, int j) {
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
  for (int i = 0; i <= b.n(); i++) {
    seeds[i].clear();
    for (const Cand& c : t.out[i][j])
      seeds[i].push_back({c.cost, openKey(c, b.aBegin + i, b.bBegin + j), DELETE, i, j, c.key});
    if (begin < 0 && !seeds[i].empty()) begin = b.aBegin + i;
  }
  if (begin < 0) return;
  del.sweep(
      SweepOps{t}, scratch, begin, b.aEnd, [&](int ri) -> const Cell& { return seeds[ri - b.aBegin]; },
      [&](int ri, const Cell& e) {
        for (const Cand& c : e) t.insert(t.out[ri - b.aBegin][j], c);
      });
}

vector<Tables> solveVimDiff(const vector<Block>& blocks, const vector<BlockCosts>& costs,
                            const FlatText& initial, const FlatText& goal, TilingCost& del,
                            int maxPlans) {
  TilingCost::Scratch<Cell> scratch = del.makeScratch<Cell>({});
  auto matches = [&](int ra, int rb) { return initial.text[ra] == goal.text[rb]; };
  vector<Tables> tables;
  for (int k = 0; k < (int)blocks.size(); k++) {
    const Block& b = blocks[k];
    const BlockCosts& c = costs[k];
    Tables t(b.n(), b.m(), maxPlans);
    vector<Cell> seeds(b.n() + 1);
    for (int j = 0; j <= b.m(); j++) {
      for (int i = 0; i <= b.n(); i++) {
        Cell& in = t.in[i][j];
        Cell& out = t.out[i][j];
        const int ra = b.aBegin + i, rb = b.bBegin + j;
        if (j > 0) {
          const int pj = j - 1;
          for (const Cand& x : t.in[i][pj]) t.relax(in, x, c.typed[pj], TYPE, i, pj, x.key);
          for (const Cand& x : t.out[i][pj])
            t.relax(in, x, c.enter[pj], ENTER, i, pj, openKey(x, ra, rb - 1));
        }
        if (i == j && i <= c.lead) {
          if (k == 0) {
            out.push_back({0.0, 0, LEADING, -1, -1, 0});
          } else {
            const Tables& pt = tables[k - 1];
            const Block& pb = blocks[k - 1];
            for (int tr = 0; tr < (int)c.cross.size(); tr++) {
              const int pi = pb.n() - tr, pj = pb.m() - tr;
              for (const Cand& x : pt.out[pi][pj])
                t.relax(out, x, c.cross[tr][i], CROSS, pi, pj,
                        closeKey(x, pb.aBegin + pi, pb.bBegin + pj));
            }
          }
        }
        for (int pi = i - 1, pj = j - 1; pi >= 0 && pj >= 0 && matches(ra - 1 - (i - 1 - pi), rb - 1 - (j - 1 - pj));
             pi--, pj--)
          for (const Cand& x : t.out[pi][pj])
            t.relax(out, x, c.move[pi][i], MOVE, pi, pj, closeKey(x, b.aBegin + pi, b.bBegin + pj));
        for (const Cand& x : in) t.relax(out, x, 0.0, EXIT, i, j, x.key);
      }
      relaxDeletes(t, b, del, scratch, seeds, j);
    }
    tables.push_back(std::move(t));
  }
  return tables;
}

// ---- Stage 4: plans --------------------------------------------------------

struct Region {  // raw spans: initial [aBegin,aEnd) -> goal [bBegin,bEnd)
  int aBegin, aEnd, bBegin, bEnd;
  bool operator==(const Region&) const = default;
};

struct RawPlan {
  vector<Region> regions;
  double cost = 0.0;
};

const Cand& predecessor(const Tables& t, const Cand& c) {
  const bool predOut = c.step != TYPE && c.step != EXIT;
  for (const Cand& p : predOut ? t.out[c.pi][c.pj] : t.in[c.pi][c.pj])
    if (p.key == c.pkey) return p;
  CHECK(false, "VimDiff: predecessor candidate missing");
  return c;
}

// A region spans from its first delete/type step to the next move (or the end).
vector<Region> walk(const vector<Tables>& tables, const vector<Block>& blocks, int k, int i, int j,
                    const Cand* c) {
  vector<Region> regions;
  int ca = blocks[k].aBegin + i, cb = blocks[k].bBegin + j;
  while (c->step != LEADING) {
    const int pk = c->step == CROSS ? k - 1 : k;
    const Cand& pred = predecessor(tables[pk], *c);
    const int pa = blocks[pk].aBegin + c->pi, pb = blocks[pk].bBegin + c->pj;
    if (c->step == MOVE || c->step == CROSS) {
      if (pred.open()) {
        ca = pa;
        cb = pb;
      }
    } else if ((c->step == DELETE || c->step == ENTER) && !pred.open()) {
      regions.push_back({pa, ca, pb, cb});
    }
    c = &pred;
    k = pk;
  }
  reverse(regions.begin(), regions.end());
  return regions;
}

// Identical-replace regions are dropped: never optimal, rejected downstream.
vector<RawPlan> reconstructPlans(const vector<Tables>& tables, const vector<Block>& blocks,
                                 int trail, const FlatText& initial, const FlatText& goal) {
  struct Top {
    double cost;
    int i, j;
    const Cand* c;
  };
  const int last = (int)blocks.size() - 1;
  const Block& b = blocks[last];
  vector<Top> tops;
  for (int t = trail; t >= 0; t--)
    for (const Cand& c : tables[last].out[b.n() - t][b.m() - t])
      if (c.step != LEADING) tops.push_back({c.cost, b.n() - t, b.m() - t, &c});
  stable_sort(tops.begin(), tops.end(),
              [](const Top& lhs, const Top& rhs) { return lhs.cost < rhs.cost; });

  vector<RawPlan> plans;
  for (const Top& top : tops) {
    if ((int)plans.size() == tables[last].maxPlans) break;
    vector<Region> regions = walk(tables, blocks, last, top.i, top.j, top.c);
    const bool degenerate = any_of(regions.begin(), regions.end(), [&](const Region& r) {
      return string_view(initial.text).substr(r.aBegin, r.aEnd - r.aBegin) ==
             string_view(goal.text).substr(r.bBegin, r.bEnd - r.bBegin);
    });
    const bool seen = any_of(plans.begin(), plans.end(),
                             [&](const RawPlan& plan) { return plan.regions == regions; });
    if (!degenerate && !seen) plans.push_back({std::move(regions), top.cost});
  }
  return plans;
}

// ---- Pipeline --------------------------------------------------------------

constexpr long long MAX_PLANNER_CELLS = 100'000'000;

struct Planned {
  FlatText initial, goal;
  Typing typing;
  vector<RawPlan> plans;
};

Planned plan(const Lines& initialLines, const Lines& goalLines, const Config& config,
             const CostOptions& options) {
  Planned p{FlatText(initialLines), FlatText(goalLines), Typing(FlatText(goalLines), config)};
  if (p.initial.text == p.goal.text) return p;
  const vector<Block> blocks =
      sealMatchedRuns(p.initial, p.goal, p.typing, initialLines, goalLines, config, options);
  long long cells = 0;
  for (const Block& b : blocks) cells += (long long)(b.n() + 1) * (b.m() + 1);
  CHECK(cells <= MAX_PLANNER_CELLS, "VimDiff diff too large for the planner DP");
  const vector<BlockCosts> costs =
      calculateTransitionCosts(p.initial, p.goal, p.typing, blocks, options);
  TilingCost del(p.initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Delete);
  const vector<Tables> tables =
      solveVimDiff(blocks, costs, p.initial, p.goal, del, max(1, options.maxPlans));
  p.plans = reconstructPlans(tables, blocks, costs.back().trail, p.initial, p.goal);
  return p;
}

DiffState diffFromRegion(const Lines& initialLines, const Planned& p, const Region& r) {
  string deletedText = p.initial.text.substr(r.aBegin, r.aEnd - r.aBegin);
  string insertedText = p.goal.text.substr(r.bBegin, r.bEnd - r.bBegin);
  CursorPos begin = DiffText::flatIndexToPosition(r.aBegin, p.initial.text);
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
  for (const RawPlan& rp : p.plans) {
    Plan result;
    result.cost = rp.cost;
    for (const Region& r : rp.regions) result.diffs.push_back(diffFromRegion(initialLines, p, r));
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
  TilingCost del(p.initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Delete);
  TilingCost move(p.initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Move);
  vector<CostBreakdown> breakdowns;
  breakdowns.reserve(p.plans.size());
  for (const RawPlan& rp : p.plans) {
    CostBreakdown bd;
    int prevEnd = -1;
    for (const Region& r : rp.regions) {
      const double delCost = del.query(r.aBegin, r.aEnd);
      const double ins =
          r.bBegin < r.bEnd ? p.typing.insertOverhead + p.typing.ins(r.bBegin, r.bEnd) : 0.0;
      const double mv = prevEnd < 0 ? 0.0 : move.query(prevEnd, r.aBegin);
      prevEnd = r.aEnd;
      bd.total += delCost + ins + mv;
      bd.regions.push_back(RegionBreakdown{diffFromRegion(initialLines, p, r), delCost, ins, mv});
    }
    breakdowns.push_back(std::move(bd));
  }
  return breakdowns;
}

}  // namespace VimDiff
