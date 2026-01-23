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

add_executable(vimficiency_tests
  # Commands - tests for command execution
  Commands/CountMotionsTest.cpp
  Commands/MiscMotions.cpp
  Commands/WordMotions.cpp
  Commands/LineMotions.cpp
  Commands/ParagraphMotions.cpp
  Commands/SentenceMotions.cpp

  # Operator - tests for operator boundary crossing (VimEndpointUtils)
  Operator/TestHelpers.cpp
  Operator/Words.cpp
  Operator/Lines.cpp
  Operator/Paragraphs.cpp
  Operator/Sentences.cpp
  Operator/TextObjects.cpp

  # EditPrimitives
  EditPrimitives/DiffStateTest.cpp

  # Misc
  Misc/ConfigurationTest.cpp
  Misc/DebugSequenceTests.cpp
  Misc/ErrorHandlingTest.cpp
  Misc/HashCollisionTest.cpp

  # Optimizer
  Optimizer/EditOptimizerTests.cpp
  Optimizer/MovementOptimizerTest.cpp
  Optimizer/CostConsistencyTests.cpp
  Optimizer/DeterminismTests.cpp
  Optimizer/HumanApprovalTests.cpp

  # Utils
  Utils/EditTestGenerators.cpp
  Utils/NeovimOracle.cpp
  Utils/TestUtils.cpp

  Debug.cpp
)

target_include_directories(vimficiency_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Operator
)

target_link_libraries(vimficiency_tests
  PRIVATE
    vimficiency_core
    GTest::gtest_main
    msgpack-cxx
)

include(GoogleTest)
gtest_discover_tests(vimficiency_tests)
