#include "SealMatchedRuns.h"

#include <vector>

#include "MyersDiff.h"

using namespace std;

namespace VimDiff {

vector<Block> sealMatchedRuns(const FlatText& initial, const FlatText& goal, const Typing& typing,
                              TilingCost& del, TilingCost& move, const Lines& initialLines,
                              const Lines& goalLines) {
  const int N = (int)initial.text.size();
  const int M = (int)goal.text.size();

  // Run chars are matched, so their retype cost is read off the goal prefix sums.
  // Not monotone in d, so the whole half is scanned.
  auto leftMargin = [&](int begin, int off, int dmax) {
    if (begin == 0 || dmax <= 0) return 0;
    vector<double> mv(dmax + 1, 0.0);
    move.sweep(begin, begin + dmax, [&](int ri, double c) { mv[ri - begin] = c; });
    const double slack = del.stopSlack(begin);
    int margin = 0;
    for (int d = 1; d <= dmax; d++)
      if (typing.ins(begin + off, begin + off + d) <= slack + mv[d]) margin = d;
    return margin;
  };
  auto rightMargin = [&](int end, int off, int dmax) {
    if (end == N || dmax <= 0) return 0;
    vector<double> mv(dmax + 1, 0.0);
    move.sweep(end - dmax, end, [&](int ri, double c) { mv[ri - (end - dmax)] = c; });
    const double slack = del.startSlack(end);
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
      // The full entry+<Esc> round trip stays the worst case despite the change
      // merge: the text right of the core may be a pure insertion, which
      // nothing merges with.
      sealed = typing.ins(coreBegin + off, coreEnd + off) >
               move.query(coreBegin, coreEnd) + typing.entry + typing.esc +
                   del.stopSlack(coreBegin) + del.startSlack(coreEnd);
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
