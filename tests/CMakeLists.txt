include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
)

FetchContent_Declare(
  msgpack-c
  GIT_REPOSITORY https://github.com/msgpack/msgpack-c.git
  GIT_TAG        cpp-7.0.0
)

FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.9.1
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)

if(VIMF_ENABLE_FUZZTEST)
  FetchContent_Declare(
    fuzztest
    GIT_REPOSITORY https://github.com/google/fuzztest.git
    GIT_TAG        2026-02-19
  )
endif()

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MSGPACK_USE_BOOST OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest msgpack-c googlebenchmark nlohmann_json)

if(VIMF_ENABLE_FUZZTEST)
  FetchContent_MakeAvailable(fuzztest)
endif()

# =============================================================================
# Shared test utilities library
# =============================================================================
# Contains common infrastructure used by multiple test binaries:
# - NeovimOracle for ground truth verification
# - Random buffer generation helpers
# - Test helper functions

add_library(test_utils STATIC
  Utils/EditTestGenerators.cpp
  Utils/NeovimOracle.cpp
  Utils/OracleReplay.cpp
  Utils/SeedManager.cpp
  Utils/TestUtils.cpp
  Operator/TestHelpers.cpp
)

target_include_directories(test_utils
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Operator
)

target_link_libraries(test_utils
  PUBLIC
    vimficiency_core
    GTest::gtest
    msgpack-cxx
)

# =============================================================================
# Unit tests binary
# =============================================================================
# Main test suite for correctness verification.
# Run with: ./vimficiency_tests

add_executable(vimficiency_tests
  TestMain.cpp
  ${PROJECT_SOURCE_DIR}/src/LuaExports/Payload.cpp

  # Commands - VimCore motion regressions, specific-output assertions, and
  # NavContext-dependent scroll tests. Bulk oracle conformance for motions
  # lives in tests/Properties/ instead.
  Commands/CountMotionsTest.cpp
  Commands/CountedMiscMotions.cpp
  Commands/MiscMotions.cpp
  Commands/ScrollMotions.cpp

  # Operator - boundary-aware delete/change unit tests and specific-output
  # text-object regressions. Bulk operator+motion oracle conformance lives in
  # tests/Properties/OperatorBoundaryProperties.cpp.
  Operator/Lines.cpp
  Operator/TextObjects.cpp

  # CompositionOptimizer
  CompositionOptimizer/DiffStateTest.cpp
  CompositionOptimizer/JoinLinesManualTest.cpp
  CompositionOptimizer/ManualTest.cpp
  CompositionOptimizer/BracketQuoteContextTest.cpp
  CompositionOptimizer/HumanApprovalTest.cpp
  CompositionOptimizer/PureInsertionManualTest.cpp
  CompositionOptimizer/PlannedEditViewTest.cpp
  CompositionOptimizer/GoalPosTest.cpp
  CompositionOptimizer/EditSequenceSpanTest.cpp
  CompositionOptimizer/TextObjectManualTest.cpp

  # TransformOptimizer
  TransformOptimizer/AutoindentManualTest.cpp
  TransformOptimizer/CountPenaltyIntegrationTest.cpp
  TransformOptimizer/ManualTest.cpp
  TransformOptimizer/ExclusiveLinewiseManualTest.cpp
  TransformOptimizer/MultipleResultsPerStartTest.cpp
  TransformOptimizer/CostConsistencyTest.cpp
  TransformOptimizer/DeterminismTest.cpp
  TransformOptimizer/HumanApprovalTest.cpp
  TransformOptimizer/RegressionTests.cpp

  # NavOptimizer
  NavOptimizer/BoundaryTest.cpp
  NavOptimizer/CountRepeatTest.cpp
  NavOptimizer/ManualTest.cpp
  NavOptimizer/CostConsistencyTest.cpp
  NavOptimizer/DeterminismTest.cpp
  NavOptimizer/HeuristicTest.cpp
  NavOptimizer/HumanApprovalTest.cpp

  # Optimizer (shared)
  Optimizer/OptimizerParamOverridesTest.cpp

  # Misc
  Misc/ConfigurationTest.cpp
  Misc/CountPenaltyFFITest.cpp
  Misc/CountPenaltyTest.cpp
  Misc/EffortCompositionTest.cpp
  Misc/ErrorHandlingTest.cpp
  Misc/HashCollisionTest.cpp
  Misc/LuaExportsPayloadTest.cpp
  Misc/NavClassMaskTest.cpp
  Misc/VimOptionsTest.cpp
  Explore/ExploreTest.cpp
  Explore/HeaderSpanTest.cpp
  Explore/NativeFlowTest.cpp
  Explore/RecommendationTest.cpp
  Explore/FixtureLoader.cpp
  Explore/FixtureIntegrationTest.cpp
  Golden/GoldenReportTest.cpp
)

target_link_libraries(vimficiency_tests
  PRIVATE
    test_utils
    GTest::gtest
    nlohmann_json::nlohmann_json
)

include(GoogleTest)
gtest_discover_tests(vimficiency_tests)

# =============================================================================
# FuzzTest generated-property/fuzz tests binary
# =============================================================================
# In unit-test mode this is the deterministic/property smoke runner. In
# FUZZTEST_FUZZING_MODE it becomes the coverage-guided fuzz campaign runner.

if(VIMF_ENABLE_FUZZTEST)
  if(FUZZTEST_FUZZING_MODE AND COMMAND fuzztest_setup_fuzzing_flags)
    fuzztest_setup_fuzzing_flags()
  endif()

  add_executable(vimficiency_fuzz_tests
    Fuzz/ParserBoundaryFuzzTest.cpp
    Fuzz/ParserPayloadFuzzTest.cpp
    Properties/CharFindMotionProperties.cpp
    Properties/CompositionOptimizerProperties.cpp
    Properties/CountedMotionProperties.cpp
    Properties/DiffStateProperties.cpp
    Properties/EffortCompositionProperties.cpp
    Properties/MotionInvariantProperties.cpp
    Properties/NavOptimizerProperties.cpp
    Properties/OperatorBoundaryProperties.cpp
    Properties/TransformOptimizerProperties.cpp
    ${PROJECT_SOURCE_DIR}/src/LuaExports/Payload.cpp
  )

  target_link_libraries(vimficiency_fuzz_tests
    PRIVATE
      test_utils
  )

  link_fuzztest(vimficiency_fuzz_tests)
  if(NOT FUZZTEST_FUZZING_MODE AND NOT FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer")
    gtest_discover_tests(vimficiency_fuzz_tests
      PROPERTIES
        ENVIRONMENT
          "FUZZTEST_FUZZ_FOR=0\\;FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk"
    )
  endif()
endif()

# =============================================================================
# Benchmarks binary
# =============================================================================
# Performance benchmarks for optimizers.
# Run with: ./vimficiency_benchmarks

add_executable(vimficiency_benchmarks
  Benchmarks/BenchmarkMain.cpp
  Benchmarks/TransformOptimizerBench.cpp
  Benchmarks/NavOptimizerBench.cpp
  Benchmarks/CompositionOptimizerBench.cpp
)

target_link_libraries(vimficiency_benchmarks
  PRIVATE
    test_utils
    benchmark::benchmark
)

# =============================================================================
# Debug binary
# =============================================================================
# Scratch tests and debugging utilities.
# Run with: ./vimficiency_debug

add_executable(vimficiency_debug
  Debug/Debug.cpp
  Debug/OldDebug.cpp
)

target_link_libraries(vimficiency_debug
  PRIVATE
    test_utils
    GTest::gtest_main
)

# =============================================================================
# Diff debug tool
# =============================================================================
# Visualize how diff planners split buffer changes into regions.
# Run with: ./vimficiency_diff_debug 'aaa\nbbb\nccc' 'ccc\nddd'

add_executable(vimficiency_diff_debug
  Debug/DiffDebug.cpp
)

target_link_libraries(vimficiency_diff_debug
  PRIVATE
    vimficiency_core
)
