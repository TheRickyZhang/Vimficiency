#pragma once

#include <optional>
#include <string_view>

#include <gtest/gtest.h>

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Mode.h"
#include "Utils/NeovimOracle.h"

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

}  // namespace OracleReplay
