cmake_minimum_required(VERSION 3.20)
project(vimficiency LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Huh what does this do?
set(CMAKE_POSITION_INDEPENDENT_CODE ON) 
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

# Produces STATIC libvimficiency_core.a.
# Note to self: Static library just produces bundled code for purposes of easier inclsusion into others
add_library(vimficiency_core STATIC
    src/Keyboard/CharacterToKeys.cpp
    src/Keyboard/SequenceTokenizer.cpp
    src/Keyboard/TemplateKeyboard.cpp
    src/Optimizer/Optimizer.cpp
    src/State/EffortState.cpp
    src/State/Snapshot.cpp
    src/State/State.cpp
    src/VimCore/VimUtils.cpp
)
# We can reference everything under src/ absolutely
target_include_directories(vimficiency_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# SHARED library for Neovim/Lua direct usage
add_library(vimficiency SHARED src/lua_exports.cpp)
target_link_libraries(vimficiency PRIVATE vimficiency_core)

# CLI for independent testing
add_executable(vimficiency_cli src/main.cpp)
target_link_libraries(vimficiency_cli PRIVATE vimficiency_core)

# Tests
enable_testing()
add_subdirectory(src/Testing)


