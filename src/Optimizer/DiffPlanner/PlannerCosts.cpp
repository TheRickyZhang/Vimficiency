#include "PlannerCosts.h"

#include <algorithm>
#include <string_view>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "VimCore/CharMask.h"

using namespace std;

namespace VimDiff {

FlatText::FlatText(const Lines& lines) : text(lines.flatten()) {
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

TilingCost::TilingCost(const FlatText& initial, double scale, int mxcnt, Kind kind)
    : N_((int)initial.text.size()), mxcnt_(mxcnt), kind_(kind),
      lineStarts_(initial.lineStarts), paraStarts_(initial.paraStarts) {
  buildBoundaries(initial.text);
  buildChunk<CountClass::EditChar, CountClass::MovementChar>(CHAR, scale);
  buildChunk<CountClass::EditWord, CountClass::MovementWord>(WORD, scale);
  buildChunk<CountClass::EditBigWord, CountClass::MovementBigWord>(BIG_WORD, scale);
  buildChunk<CountClass::EditLine, CountClass::MovementLine>(LINE, scale);
  buildChunk<CountClass::EditParagraph, CountClass::MovementParagraph>(PARAGRAPH, scale);
  toLineEnd_.kind = CHUNK_LINE_END;
  toLineStart_.kind = CHUNK_LINE_START;
  toLineEnd_.base = TO_LINE_END_KEYS * scale;
  toLineStart_.base = TO_LINE_START_KEYS * scale;
  toLineEnd_.pen = toLineStart_.pen = {0.0, 0.0};
  for (vector<Arrival>* v : {&reach_, &wordStart_, &bigStart_, &wsStart_}) v->assign(N_ + 1, {});
  lineMin_.assign(lineStarts_.size(), {});
  paraMin_.assign(paraStarts_.size(), {});
  for (Chunk& c : chunk_) {
    c.gap = c.base;
    for (int s = 2; s <= mxcnt_; s++)
      for (int a = 1; a <= s / 2; a++)
        c.gap = max(c.gap, c.base + c.pen[a] + c.pen[s - a] - c.pen[s]);
  }
}

double TilingCost::coverBound(int x, int y) const {
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

double TilingCost::chainCost(Level level, int units) const {
  if (units <= 0) return 0.0;
  return ((units + mxcnt_ - 1) / mxcnt_) * chunk_[level].cost(min(units, mxcnt_));
}

double TilingCost::stopSlack(int edge) const {
  const double cover = coverBound(lineStartOf_[edge], edge);
  const int line = lineOf(edge);
  const int paraFirstLine = lineOf(paraStarts_[paraOf(edge)]);
  const int j = line - paraFirstLine;
  double slack = maxChunkGap();
  slack = max(slack, chunk_[LINE].gap + cover);
  slack = max(slack, chunk_[PARAGRAPH].gap + chainCost(LINE, j) + cover);
  return slack;
}

double TilingCost::startSlack(int edge) const {
  const int line = lineOf(edge);
  const int nextStart = lineStarts_[min(line + 1, (int)lineStarts_.size() - 1)];
  const double toEol = toLineEnd_.base + chunk_[CHAR].cost(1);
  const int nextPara = paraStarts_[min(paraOf(edge) + 1, (int)paraStarts_.size() - 1)];
  const int jTail = max(0, lineOf(nextPara) - line - 1);
  double slack = maxChunkGap();
  slack = max(slack, coverBound(edge, nextStart));
  slack = max(slack, toEol + chunk_[LINE].gap);
  slack = max(slack, toEol + chunk_[PARAGRAPH].gap + chainCost(LINE, jTail));
  return slack;
}

double TilingCost::query(int begin, int end) {
  double cost = 0.0;
  sweep(begin, end, [&](int ri, double c) { if (ri == end) cost = c; });
  return cost;
}

vector<TilingChunk> TilingCost::cover(int begin, int end) {
  vector<Arrival> arrivals(end - begin + 1);
  sweep(begin, end, [&](int ri) { return ri == begin ? 0.0 : INF; },
        [&](int ri, const Arrival& a) { arrivals[ri - begin] = a; });
  vector<TilingChunk> chunks;
  for (int pos = end; pos > begin;) {
    const Arrival& a = arrivals[pos - begin];
    const double base = a.from == begin ? 0.0 : arrivals[a.from - begin].cost;
    chunks.push_back({a.from, pos, a.kind, a.count, a.cost - base});
    pos = a.from;
  }
  reverse(chunks.begin(), chunks.end());
  return chunks;
}

double TilingCost::maxChunkGap() const {
  return max({chunk_[CHAR].gap, chunk_[WORD].gap, chunk_[BIG_WORD].gap});
}

int TilingCost::lineOf(int ri) const {
  return (int)(upper_bound(lineStarts_.begin(), lineStarts_.end(), ri) -
               lineStarts_.begin()) - 1;
}

int TilingCost::paraOf(int ri) const {
  return (int)(upper_bound(paraStarts_.begin(), paraStarts_.end(), ri) -
               paraStarts_.begin()) - 1;
}

void TilingCost::relaxRuns(Arrival& e, int begin, int ri, const Chunk& chunk,
                           const vector<char>& isClass, const vector<int>& idx,
                           const vector<int>& starts, const vector<int>& ends,
                           const vector<Arrival>& runStart) const {
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
  for (int k = 1; k <= mxcnt_ && runIdx - k + 1 >= 0; k++) {
    const int run = runIdx - k + 1;
    if (ends[run] <= begin) break;
    const int runBegin = starts[run];
    relax(e, runStart[runBegin], chunk, k);
    if (runBegin > begin && isWs_[runBegin - 1] && k + endsInWs <= mxcnt_)
      relax(e, wsStart_[wsRunStart_[runBegin - 1]], chunk, k + endsInWs);
  }
}

void TilingCost::relaxUnits(Arrival& e, int begin, int ri, const Chunk& chunk,
                            const vector<int>& idxAt, const vector<int>& starts,
                            const vector<Arrival>& unitMin) const {
  if (idxAt[ri] < 0) return;
  const int unit = idxAt[ri];
  for (int u = unit - 1; u >= 0 && unit - u <= mxcnt_; u--) {
    if (kind_ == Kind::Delete) {
      if (starts[u] < begin) break;
      relax(e, reach_[starts[u]], chunk, unit - u);
    } else {
      if (starts[u + 1] <= begin) break;
      relax(e, unitMin[u], chunk, unit - u);
    }
  }
}

void TilingCost::buildBoundaries(const string& text) {
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

Typing::Typing(const FlatText& goal, const Config& config)
    : PS(goal.text.size() + 1, 0.0),
      entry(getEffort("i", config)), esc(getEffort("<Esc>", config)) {
  for (int rj = 0; rj < (int)goal.text.size(); rj++) {
    KeyedSequence one;
    one.append(string_view(goal.text).substr(rj, 1));
    PS[rj + 1] = PS[rj] + RunningEffort(one.keys, config).getEffort(config);
  }
}

}  // namespace VimDiff
