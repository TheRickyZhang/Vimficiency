#pragma once

#include <optional>
#include <string_view>

#include <gtest/gtest.h>

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Mode.h"
#include "Utils/NeovimOracle.h"

// Note we keep this seprate from NeovimOracle since we often import oracle without these replay helpers
namespace OracleReplay {

::testing::AssertionResult matches(
    NeovimOracle& oracle,
    const Lines& initial,
    CursorPos initialPos,
    std::string_view sequence,
    const Lines& goal,
    std::optional<CursorPos> goalPos = std::nullopt,
    std::optional<Mode> goalMode = Mode::Normal,
    std::string_view context = {});

void expectMatchesOracle(
    NeovimOracle& oracle,
    const Lines& initial,
    CursorPos initialPos,
    std::string_view sequence,
    const Lines& expectedLines,
    CursorPos expectedPos,
    Mode expectedMode = Mode::Normal,
    std::string_view context = {});

}  // namespace OracleReplay
