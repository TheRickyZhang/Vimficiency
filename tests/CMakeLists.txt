include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(vimficiency_tests
  Actions/CountMotionsTest.cpp
  Actions/EditTest.cpp
  Actions/MotionTest.cpp
  EditPrimitives/DiffStateTest.cpp
  EditPrimitives/LevenshteinTest.cpp
  Misc/ConfigurationTest.cpp
  Misc/DebugSequenceTests.cpp
  Misc/ErrorHandlingTest.cpp
  Misc/HashCollisionTest.cpp
  Optimizer/EditOptimizerTests.cpp
  Optimizer/MovementOptimizerTest.cpp
  Reach/BackwardReachTest.cpp
  Reach/ForwardReachTest.cpp
  Utils/TestUtils.cpp
  Temp.cpp
)

target_include_directories(vimficiency_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vimficiency_tests
  PRIVATE
    vimficiency_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(vimficiency_tests)
