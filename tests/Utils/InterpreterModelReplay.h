#pragma once

#include <string_view>

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Mode.h"

struct InterpreterReplayResult {
  Lines lines;
  CursorPos cursor;
  Mode mode;
};

InterpreterReplayResult applyUserSequence(
    Lines lines, CursorPos cursor, std::string_view sequence);
