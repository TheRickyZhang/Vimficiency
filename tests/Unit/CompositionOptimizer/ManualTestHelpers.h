#pragma once

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Mode.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"

class CompositionOptimizer_ManualTest : public ::testing::Test {
protected:
  inline static std::unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  CompositionOptimizer opt{config};
  CompositionOptimizerParams params{};

  static void SetUpTestSuite() { oracle = std::make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  void verifySingleResult(
      const Result& result,
      const Lines& initial, CursorPos initialPos,
      const Lines& goal,
      const std::string& context = "") {
    EXPECT_TRUE(OracleReplay::matches(
        *oracle, initial, initialPos, result.getSequence().str(),
        goal, std::nullopt, Mode::Normal, context));
  }

  void expectHasValidResults(
      const std::vector<Result>& results,
      const Lines& initial,
      CursorPos initialPos,
      const Lines& goal,
      const std::string& testContext = "") {

    ASSERT_FALSE(results.empty())
        << "No results returned" << (testContext.empty() ? "" : " (" + testContext + ")");

    for (size_t i = 0; i < results.size(); i++) {
      const auto& seq = results[i].getSequence();
      EXPECT_TRUE(OracleReplay::matches(
          *oracle, initial, initialPos, seq.str(),
          goal, std::nullopt, Mode::Normal,
          testContext.empty() ? "result " + std::to_string(i)
                              : testContext + " result " + std::to_string(i)));
    }
  }
};
