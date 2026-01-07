cmake_minimum_required(VERSION 3.20)
project(vimficiency LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON) 
set(CMAKE_CXX_EXTENSIONS OFF)

# Have debug enabled by default for development
option(VIMFICIENCY_DEBUG "Enable debug output" ON)

# Legacy Vim compatibility mode (uses Vim defaults instead of Neovim defaults)
# Key differences when enabled:
#   - startofline: ON (cursor goes to first non-blank on gg/G/dd/etc)
#   - joinspaces: ON (two spaces after .!? when joining lines)
#   - Y: yy behavior (yank line) instead of y$ (yank to EOL)
option(VIMFICIENCY_LEGACY_VIM "Use legacy Vim defaults instead of Neovim defaults" OFF)

# Produces STATIC libvimficiency_core.a.
# Note to self: Static library just produces bundled code for purposes of easier inclsusion into others

# NOTE Remove configure_depends when convenience no longer needed in development
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    src/Editor/*.cpp
    src/Keyboard/*.cpp
    src/Optimizer/*.cpp
    src/State/*.cpp
    src/Utils/*.cpp
    src/VimCore/*.cpp
)
add_library(vimficiency_core STATIC ${CORE_SOURCES})

# We can reference everything under src/ absolutely
target_include_directories(vimficiency_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(VIMFICIENCY_DEBUG)
    target_compile_definitions(vimficiency_core PUBLIC VIMFICIENCY_DEBUG)
endif()

if(VIMFICIENCY_LEGACY_VIM)
    target_compile_definitions(vimficiency_core PUBLIC VIMFICIENCY_LEGACY_VIM)
endif()

# SHARED library for Neovim/Lua direct usage
add_library(vimficiency SHARED src/lua_exports.cpp)
target_link_libraries(vimficiency PRIVATE vimficiency_core)

# CLI for independent testing
add_executable(vimficiency_cli src/main.cpp)
target_link_libraries(vimficiency_cli PRIVATE vimficiency_core)

# Tests
enable_testing()
add_subdirectory(tests)


