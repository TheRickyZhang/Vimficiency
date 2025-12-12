cmake_minimum_required(VERSION 3.20)
project(vimficiency LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(vimficiency ${SOURCES})

# so #include "foo.h" works from any source file
target_compile_definitions(vimficiency
    PRIVATE
        TESTFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/TestFiles"
)
