include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(vimficiency_tests
  OptimizerTest.cpp
  MotionTest.cpp
  ConfigurationTest.cpp
  CountMotionsTest.cpp
  DebugSequenceTests.cpp
  ErrorHandlingTest.cpp
  LevenshteinTest.cpp
  TestUtils.cpp
)

target_link_libraries(vimficiency_tests
  PRIVATE
    vimficiency_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(vimficiency_tests)
