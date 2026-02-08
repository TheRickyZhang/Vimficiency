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

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MSGPACK_USE_BOOST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest msgpack-c)

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

  # EditOptimizer
  EditOptimizer/ManualTest.cpp
  EditOptimizer/OutputCorrectnessTest.cpp
  EditOptimizer/CostConsistencyTest.cpp
  EditOptimizer/DeterminismTest.cpp
  EditOptimizer/HumanApprovalTest.cpp

  # MotionOptimizer
  MotionOptimizer/ManualTest.cpp
  MotionOptimizer/OutputCorrectnessTest.cpp
  MotionOptimizer/CostConsistencyTest.cpp
  MotionOptimizer/DeterminismTest.cpp
  MotionOptimizer/HumanApprovalTest.cpp

  # Misc
  Misc/ConfigurationTest.cpp
  Misc/EffortCacheTest.cpp
  Misc/EffortCompositionTest.cpp
  Misc/ErrorHandlingTest.cpp
  Misc/HashCollisionTest.cpp
  Misc/MotionClassMaskTest.cpp
  Misc/VimOptionsTest.cpp
)

target_link_libraries(vimficiency_tests
  PRIVATE
    test_utils
    GTest::gtest
)

include(GoogleTest)
gtest_discover_tests(vimficiency_tests)

# =============================================================================
# Benchmarks binary
# =============================================================================
# Performance benchmarks for optimizers.
# Run with: ./vimficiency_benchmarks

add_executable(vimficiency_benchmarks
  Benchmarks/BenchUtils.cpp
  Benchmarks/EditOptimizerBench.cpp
  Benchmarks/MotionOptimizerBench.cpp
  Benchmarks/MotionTemplateSpecializationBench.cpp
  Benchmarks/AlternateImplementations.cpp
)

target_link_libraries(vimficiency_benchmarks
  PRIVATE
    test_utils
    GTest::gtest_main
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
