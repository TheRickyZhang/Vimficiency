#include "SealMatchedRuns.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "MyersDiff.h"

using namespace std;

namespace VimDiff {

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

}  // namespace VimDiff
