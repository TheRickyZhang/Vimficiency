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

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MSGPACK_USE_BOOST OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest msgpack-c googlebenchmark nlohmann_json)

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

  # Commands - VimCore motion correctness
  Commands/CountMotionsTest.cpp
  Commands/MiscMotions.cpp
  Commands/WordMotions.cpp
  Commands/LineMotions.cpp
  Commands/ParagraphMotions.cpp
  Commands/SentenceMotions.cpp

  # Operator - VimCore edit/delete correctness
  Operator/Words.cpp
  Operator/Lines.cpp
  Operator/Paragraphs.cpp
  Operator/Sentences.cpp
  Operator/TextObjects.cpp

  # CompositionOptimizer
  CompositionOptimizer/DiffStateTest.cpp
  CompositionOptimizer/ManualTest.cpp
  CompositionOptimizer/OutputCorrectnessTest.cpp
  CompositionOptimizer/BracketQuoteContextTest.cpp
  CompositionOptimizer/HumanApprovalTest.cpp
  CompositionOptimizer/StepViewTest.cpp

  # TransformOptimizer
  TransformOptimizer/CountPenaltyIntegrationTest.cpp
  TransformOptimizer/ManualTest.cpp
  TransformOptimizer/MultipleResultsPerStartTest.cpp
  TransformOptimizer/OutputCorrectnessTest.cpp
  TransformOptimizer/CostConsistencyTest.cpp
  TransformOptimizer/DeterminismTest.cpp
  TransformOptimizer/HumanApprovalTest.cpp
  TransformOptimizer/RegressionTests.cpp

  # NavOptimizer
  NavOptimizer/ManualTest.cpp
  NavOptimizer/OutputCorrectnessTest.cpp
  NavOptimizer/CostConsistencyTest.cpp
  NavOptimizer/DeterminismTest.cpp
  NavOptimizer/HeuristicTest.cpp
  NavOptimizer/HumanApprovalTest.cpp

  # Misc
  Misc/ConfigurationTest.cpp
  Misc/CountPenaltyFFITest.cpp
  Misc/CountPenaltyTest.cpp
  Misc/EffortCompositionTest.cpp
  Misc/ErrorHandlingTest.cpp
  Misc/HashCollisionTest.cpp
  Misc/NavClassMaskTest.cpp
  Misc/VimOptionsTest.cpp
  Explore/ExploreTest.cpp
  Explore/FixtureLoader.cpp
  Explore/FixtureIntegrationTest.cpp
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
# Exploration collector binary
# =============================================================================
# Collects A* explored states for dashboard visualization.
# Run with: VIMFICIENCY_SEED_MODE=fixed ./vimficiency_explore
#
# Only configured when VIMF_TRACK_STATES=ON: the binary's whole job is to
# emit explored-state traces, and without tracking it trips a static_assert
# in ExplorationCollector.cpp. Gating the target here (rather than letting
# the assert fire) keeps the default `cmake --build build` clean.
if(VIMF_TRACK_STATES)
  add_executable(vimficiency_explore
    Exploration/SequenceChunker.cpp
    Exploration/ExplorationCollector.cpp
  )

  target_link_libraries(vimficiency_explore
    PRIVATE
      test_utils
  )
endif()

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
# Visualize how Myers diff splits buffer changes into regions.
# Run with: ./vimficiency_diff_debug 'aaa\nbbb\nccc' 'ccc\nddd'

add_executable(vimficiency_diff_debug
  Debug/DiffDebug.cpp
)

target_link_libraries(vimficiency_diff_debug
  PRIVATE
    vimficiency_core
)
