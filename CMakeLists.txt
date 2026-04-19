cmake_minimum_required(VERSION 3.20)
project(vimficiency LANGUAGES CXX)

# Default CMAKE_BUILD_TYPE to Debug for local iteration when none is specified.
# Single-config generators (Make, Ninja) leave `CMAKE_BUILD_TYPE` empty by
# default, which gives an undefined flag set — no `-O`, no `-DNDEBUG`. That's
# a footgun: it hides bugs where `assert(x)` bodies reference nonexistent
# symbols (NDEBUG would discard `x` before type-checking), and it makes local
# behavior depend on whatever build type happened to be cached last.
#
# Skipping for multi-config generators (Visual Studio, Xcode), where
# CMAKE_CONFIGURATION_TYPES drives the choice per-build instead.
#
# CI overrides this with `-DCMAKE_BUILD_TYPE=Release` explicitly, so this
# only affects bare `cmake -B build` invocations by developers.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Debug")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON) 
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Have debug enabled by default for development
option(VIMF_DEBUG "Enable debug output" ON)

# Compile in explored-state tracking and payloads (for exploration tooling)
option(VIMF_DETAILED_DEBUG_TRACKING "Enable explored-state tracking instrumentation" OFF)

# Legacy Vim compatibility mode (uses Vim defaults instead of Neovim defaults)
# Key differences when enabled:
#   - startofline: ON (cursor goes to first non-blank on gg/G/dd/etc)
#   - joinspaces: ON (two spaces after .!? when joining lines)
#   - Y: yy behavior (yank line) instead of y$ (yank to EOL)
option(VIMF_LEGACY_VIM "Use legacy Vim defaults instead of Neovim defaults" OFF)

# Produces STATIC libvimficiency_core.a.
# Note to self: Static library just produces bundled code for purposes of easier inclsusion into others

# NOTE Remove configure_depends when convenience no longer needed in development
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    src/Boundary/*.cpp
    src/Effort/*.cpp
    src/Interpreter/*.cpp
    src/Keyboard/*.cpp
    src/Optimizer/*.cpp
    src/Session/*.cpp
    src/Utils/*.cpp
    src/VimCore/*.cpp
    src/Types/*.cpp
)
add_library(vimficiency_core STATIC ${CORE_SOURCES})

# We can reference everything under src/ absolutely
target_include_directories(vimficiency_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Compile-definition mapping from CMake options to preprocessor symbols.
target_compile_definitions(vimficiency_core PUBLIC
    $<$<BOOL:${VIMF_DEBUG}>:VIMF_DEBUG>
    $<$<BOOL:${VIMF_DETAILED_DEBUG_TRACKING}>:VIMF_DETAILED_DEBUG_TRACKING>
    $<$<BOOL:${VIMF_LEGACY_VIM}>:VIMF_LEGACY_VIM>
)

# SHARED library for Neovim/Lua direct usage
add_library(vimficiency SHARED
    src/lua_exports.cpp
    src/LuaExports/AnalyzeExports.cpp
    src/LuaExports/UtilityExports.cpp
)
target_link_libraries(vimficiency PRIVATE vimficiency_core)

# CLI for independent testing
add_executable(vimficiency_cli src/main.cpp)
target_link_libraries(vimficiency_cli PRIVATE vimficiency_core)

# Tests
enable_testing()
add_subdirectory(tests)
