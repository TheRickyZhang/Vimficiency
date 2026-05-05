#pragma once

#include <cassert>

#include "Types/CharInterval.h"
#include "Types/Lines.h"

// CharRange → CharInterval conversion now lives on CharInterval as a
// constructor (see Types/CharInterval.h). This file only carries the
// whole-line helper that doesn't fit the constructor shape.

inline CharInterval wholeLineMotionInterval(const Lines& lines, int line) {
  assert(line >= 0 && line < static_cast<int>(lines.size()));
  return CharInterval(CursorPos(line, 0), CursorPos(line, lines[line].lastCol()));
}
