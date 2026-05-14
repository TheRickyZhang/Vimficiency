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

  # Commands - VimCore motion correctness
  Commands/CharFindMotions.cpp
  Commands/CountMotionsTest.cpp
  Commands/CountedMiscMotions.cpp
  Commands/MiscMotions.cpp
  Commands/ScrollMotions.cpp
  Commands/WordMotions.cpp
  Commands/LineMotions.cpp
  Commands/ParagraphMotions.cpp
  Commands/SentenceMotions.cpp

  # Operator - VimCore edit/delete correctness
  Operator/Words.cpp
  Operator/Lines.cpp
  Operator/ChangeMotions.cpp
  Operator/Paragraphs.cpp
  Operator/Sentences.cpp
  Operator/TextObjects.cpp

  # CompositionOptimizer
  CompositionOptimizer/DiffStateTest.cpp
  CompositionOptimizer/JoinLinesManualTest.cpp
  CompositionOptimizer/ManualTest.cpp
  CompositionOptimizer/OutputCorrectnessTest.cpp
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
  TransformOptimizer/OutputCorrectnessTest.cpp
  TransformOptimizer/CostConsistencyTest.cpp
  TransformOptimizer/DeterminismTest.cpp
  TransformOptimizer/HumanApprovalTest.cpp
  TransformOptimizer/RegressionTests.cpp

  # NavOptimizer
  NavOptimizer/BoundaryTest.cpp
  NavOptimizer/CountRepeatTest.cpp
  NavOptimizer/ManualTest.cpp
  NavOptimizer/OutputCorrectnessTest.cpp
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
# Fuzz/property tests binary
# =============================================================================
# Opt-in only. Gtest-compatible mode is CTest-discoverable; coverage-guided
# modes are manual because discovery executes the fuzz binary during the build.

if(VIMF_ENABLE_FUZZTEST)
  if(FUZZTEST_FUZZING_MODE AND COMMAND fuzztest_setup_fuzzing_flags)
    fuzztest_setup_fuzzing_flags()
  endif()

  add_executable(vimficiency_fuzz_tests
    Fuzz/ParserPayloadFuzzTest.cpp
    ${PROJECT_SOURCE_DIR}/src/LuaExports/Payload.cpp
  )

  target_link_libraries(vimficiency_fuzz_tests
    PRIVATE
      vimficiency_core
  )

  link_fuzztest(vimficiency_fuzz_tests)
  if(NOT FUZZTEST_FUZZING_MODE AND NOT FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer")
    gtest_discover_tests(vimficiency_fuzz_tests)
  endif()
endif()

# =============================================================================
# Raw byte fuzzers
# =============================================================================
# Opt-in libFuzzer targets for hostile byte inputs. These are intentionally not
# CTest-discovered; run them manually with libFuzzer flags in a dedicated build.

if(VIMF_ENABLE_LIBFUZZER)
  function(add_vimficiency_raw_fuzzer target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target}
      PRIVATE
        vimficiency_core
    )
    target_compile_options(${target}
      PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(${target}
      PRIVATE
        -fsanitize=fuzzer,address,undefined
    )
  endfunction()

  add_vimficiency_raw_fuzzer(vimficiency_fuzz_payload
    Fuzz/RawPayloadFuzzer.cpp
    ${PROJECT_SOURCE_DIR}/src/LuaExports/Payload.cpp
  )

  add_vimficiency_raw_fuzzer(vimficiency_fuzz_sequence_parser
    Fuzz/RawSequenceParserFuzzer.cpp
  )

  add_vimficiency_raw_fuzzer(vimficiency_fuzz_movement_parser
    Fuzz/RawMovementParserFuzzer.cpp
  )

  add_vimficiency_raw_fuzzer(vimficiency_fuzz_edit_parser
    Fuzz/RawEditParserFuzzer.cpp
  )

  add_vimficiency_raw_fuzzer(vimficiency_fuzz_snapshot
    Fuzz/RawSnapshotFuzzer.cpp
  )

  add_custom_target(vimficiency_raw_fuzzers
    DEPENDS
      vimficiency_fuzz_payload
      vimficiency_fuzz_sequence_parser
      vimficiency_fuzz_movement_parser
      vimficiency_fuzz_edit_parser
      vimficiency_fuzz_snapshot
  )
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
    Exploration/CompositionCases.cpp
    Exploration/CompositionJsonWriter.cpp
    Exploration/EditCases.cpp
    Exploration/SequenceChunker.cpp
    Exploration/ExplorationCollector.cpp
    Exploration/ExplorationJsonWriter.cpp
    Exploration/MotionCases.cpp
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
