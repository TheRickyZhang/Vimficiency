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

set(VIMFY_TEST_MAIN_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/TestMain.cpp)
set(VIMFY_LUA_PAYLOAD_SOURCE ${PROJECT_SOURCE_DIR}/src/LuaExports/Payload.cpp)

# =============================================================================
# Shared test utilities library
# =============================================================================
# Contains common infrastructure used by multiple test binaries:
# - NeovimOracle for ground truth verification
# - Random buffer generation helpers
# - Test helper functions

file(GLOB VIMFY_TEST_UTIL_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/Utils/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Operator/TestHelpers.cpp
)
list(APPEND VIMFY_TEST_UTIL_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/TransformOptimizer/EmbeddedRegionTestUtils.cpp
)

add_library(test_utils STATIC
  ${VIMFY_TEST_UTIL_SOURCES}
)

target_include_directories(test_utils
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Operator
)

target_link_libraries(test_utils
  PUBLIC
    vimfy_core
    GTest::gtest
    msgpack-cxx
)

# =============================================================================
# Unit and expect test binaries
# =============================================================================
# Unit tests cover deterministic C++ correctness. Expect tests cover curated
# report fixtures and are separate so methodology-specific runners stay simple.

file(GLOB_RECURSE VIMFY_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)
list(FILTER VIMFY_UNIT_TEST_SOURCES EXCLUDE REGEX [[/tests/(Benchmarks|Debug|Expect|Property|Safety|Utils)/]])
list(REMOVE_ITEM VIMFY_UNIT_TEST_SOURCES
  ${VIMFY_TEST_MAIN_SOURCE}
  ${CMAKE_CURRENT_SOURCE_DIR}/Operator/TestHelpers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/TransformOptimizer/EmbeddedRegionTestUtils.cpp
)

add_executable(vimfy_unit_tests
  ${VIMFY_TEST_MAIN_SOURCE}
  ${VIMFY_LUA_PAYLOAD_SOURCE}
  ${VIMFY_UNIT_TEST_SOURCES}
)

target_link_libraries(vimfy_unit_tests
  PRIVATE
    test_utils
    GTest::gtest
    nlohmann_json::nlohmann_json
)

include(GoogleTest)
gtest_discover_tests(vimfy_unit_tests)

file(GLOB VIMFY_EXPECT_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/Expect/*.cpp
)

add_executable(vimfy_expect_tests
  ${VIMFY_TEST_MAIN_SOURCE}
  ${VIMFY_EXPECT_TEST_SOURCES}
)

target_link_libraries(vimfy_expect_tests
  PRIVATE
    test_utils
    GTest::gtest
    nlohmann_json::nlohmann_json
)

gtest_discover_tests(vimfy_expect_tests)

# =============================================================================
# FuzzTest generated-property and safety test binaries
# =============================================================================
# Property tests check semantic invariants over structured project inputs. Safety
# checks adversarial or malformed inputs at parser/payload/file boundaries.

if(VIMF_ENABLE_FUZZTEST)
  if(FUZZTEST_FUZZING_MODE AND COMMAND fuzztest_setup_fuzzing_flags)
    fuzztest_setup_fuzzing_flags()
  endif()

  file(GLOB VIMFY_PROPERTY_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Property/*.cpp
  )

  add_executable(vimfy_property_tests
    ${VIMFY_PROPERTY_TEST_SOURCES}
  )

  target_link_libraries(vimfy_property_tests
    PRIVATE
      test_utils
  )

  link_fuzztest(vimfy_property_tests)
  if(NOT FUZZTEST_FUZZING_MODE AND NOT FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer")
    gtest_discover_tests(vimfy_property_tests
      PROPERTIES
        ENVIRONMENT
          "FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk"
    )
  endif()

  file(GLOB VIMFY_SAFETY_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Safety/*.cpp
  )

  add_executable(vimfy_safety_tests
    ${VIMFY_SAFETY_TEST_SOURCES}
    ${VIMFY_LUA_PAYLOAD_SOURCE}
  )

  target_link_libraries(vimfy_safety_tests
    PRIVATE
      test_utils
  )

  link_fuzztest(vimfy_safety_tests)
  if(NOT FUZZTEST_FUZZING_MODE AND NOT FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer")
    gtest_discover_tests(vimfy_safety_tests
      PROPERTIES
        ENVIRONMENT
          "FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk"
    )
  endif()
endif()

# Build target for the fast correctness surface. Running still goes through
# scripts/vimfy_tests so seed/env handling stays centralized.
set(VIMFY_FAST_TEST_TARGETS
  vimfy_unit_tests
  vimfy_expect_tests
  vimficiency
)

if(VIMF_ENABLE_FUZZTEST)
  list(APPEND VIMFY_FAST_TEST_TARGETS
    vimfy_property_tests
    vimfy_safety_tests
  )
endif()

add_custom_target(vimfy_tests
  DEPENDS ${VIMFY_FAST_TEST_TARGETS}
)

# =============================================================================
# Benchmarks binary
# =============================================================================
# Performance benchmarks for optimizers.
# Run with: ./vimfy_benchmarks

file(GLOB VIMFY_BENCHMARK_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks/*.cpp
)

add_executable(vimfy_benchmarks
  ${VIMFY_BENCHMARK_SOURCES}
)

target_link_libraries(vimfy_benchmarks
  PRIVATE
    test_utils
    benchmark::benchmark
)

# =============================================================================
# Debug binary
# =============================================================================
# Scratch tests and debugging utilities.
# Run with: ./vimfy_debug

file(GLOB VIMFY_DEBUG_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/Debug/*.cpp
)
list(REMOVE_ITEM VIMFY_DEBUG_TEST_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/Debug/DiffDebug.cpp
)

add_executable(vimfy_debug
  ${VIMFY_DEBUG_TEST_SOURCES}
)

target_link_libraries(vimfy_debug
  PRIVATE
    test_utils
    GTest::gtest_main
)

# =============================================================================
# Diff debug tool
# =============================================================================
# Visualize how diff planners split buffer changes into regions.
# Run with: ./vimfy_diff_debug 'aaa\nbbb\nccc' 'ccc\nddd'

add_executable(vimfy_diff_debug
  Debug/DiffDebug.cpp
)

target_link_libraries(vimfy_diff_debug
  PRIVATE
    vimfy_core
)
