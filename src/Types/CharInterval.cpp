#include "Types/CharInterval.h"

#include <cassert>

#include "Types/CharRange.h"
#include "Types/Lines.h"

CharInterval::CharInterval(const CharRange& range, const Lines& lines)
    : first(range.begin),
      last(lines.getPrevPos(range.end)) {
  assert(range.isValid() && !range.isEmpty());
  if (last < first) first = last;
}
