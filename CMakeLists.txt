cmake_minimum_required(VERSION 3.20)
project(vimficiency VERSION 0.1.1 LANGUAGES CXX)

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
# CI and scripts/install.sh pass `-DCMAKE_BUILD_TYPE=Release` explicitly, so
# this only affects bare `cmake -B build` invocations by developers.
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

# Off by default so plugin installs build only the shared library: the test
# tree fetches six third-party dependencies (see tests/CMakeLists.txt).
option(VIMF_BUILD_TESTS "Build test binaries and fetch their dependencies" OFF)

# Compile in explored-state tracking for the A* exploration tooling (off by
# default — enabling this adds per-state allocations on every popped node).
option(VIMF_TRACK_STATES "Enable explored-state recording for vimfy_explore" OFF)

# FuzzTest-backed property and safety tests are part of the normal test build.
# Coverage-guided campaigns are enabled separately with FUZZTEST_FUZZING_MODE
# (Clang only) — see dev/tests/fuzzing.md. The macro that sets FuzzTest's
# fuzzing flags (fuzztest_setup_fuzzing_flags) only exists after the fuzztest
# fetch in tests/, which runs *after* vimfy_core is defined below — so the code
# under test would not get coverage instrumentation. We therefore apply the
# flags here at root scope, before any target, so vimfy_core is instrumented
# too. Keep in sync with fuzztest's cmake/FuzzTestFlagSetup.cmake.
option(FUZZTEST_FUZZING_MODE "Build fuzz tests in coverage-guided fuzzing mode (Clang + ASan + sancov)" OFF)
if(FUZZTEST_FUZZING_MODE)
  set(_vimfy_fuzzing_flags "-g -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -UNDEBUG -fsanitize=address -fsanitize-coverage=inline-8bit-counters -fsanitize-coverage=trace-cmp -DADDRESS_SANITIZER")
  string(APPEND CMAKE_C_FLAGS " ${_vimfy_fuzzing_flags}")
  string(APPEND CMAKE_CXX_FLAGS " ${_vimfy_fuzzing_flags}")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=address")
endif()

# Legacy Vim compatibility mode (uses Vim defaults instead of Neovim defaults)
# Key differences when enabled:
#   - startofline: ON (cursor goes to first non-blank on gg/G/dd/etc)
#   - joinspaces: ON (two spaces after .!? when joining lines)
#   - Y: yy behavior (yank line) instead of y$ (yank to EOL)
option(VIMF_LEGACY_VIM "Use legacy Vim defaults instead of Neovim defaults" OFF)

# Produces STATIC libvimfy_core.a.
# Note to self: Static library just produces bundled code for purposes of easier inclsusion into others

# Generate a single header carrying every build-time flag. Source code reads
# these via `#if` against a name that CMake *actually* emits, eliminating the
# class of bug where `-DFOO=ON` on the command line silently fails to drive a
# `#ifdef FOO` in source (CMake accepts unknown `-D` cache variables).
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BuildConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/BuildConfig.h
    @ONLY
)

# Hash Api.def at configure time and embed it into the .so so the Lua loader
# can detect a stale binary against newer FFI bindings. Editing Api.def must
# trigger a reconfigure for the hash to refresh, hence CMAKE_CONFIGURE_DEPENDS.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    src/LuaExports/Api.def
)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/LuaExports/Api.def VF_API_DEF_HASH)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AbiHash.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/AbiHash.h
    @ONLY
)

# Planner-only WebAssembly module for the docs site's interactive explainer
# (dev/web/how-it-works-site.md). Needs the Emscripten toolchain:
#   emcmake cmake -B build-wasm -DVIMF_WASM=ON -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-wasm --target vimfy_planner_wasm
# Explicit source list rather than the glob: the module must stay the planner
# closure (no Lua exports, CLI, fast_float, or tests), and growing it should be
# a visible decision.
option(VIMF_WASM "Build the planner-only WebAssembly module (requires emcmake)" OFF)
if(VIMF_WASM)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "VIMF_WASM needs the Emscripten toolchain: emcmake cmake -B build-wasm -DVIMF_WASM=ON")
  endif()
  add_executable(vimfy_planner_wasm
    src/Wasm/PlannerExports.cpp
    src/Optimizer/DiffPlanner/VimDiff.cpp
    src/Optimizer/DiffPlanner/PlannerCosts.cpp
    src/Optimizer/DiffPlanner/SealMatchedRuns.cpp
    src/Optimizer/DiffPlanner/MyersDiff.cpp
    src/Optimizer/DiffPlanner/DiffState.cpp
    src/Boundary/TransformBoundary.cpp
    src/Boundary/BoundaryContext.cpp
    src/Effort/RunningEffort.cpp
    src/Keyboard/Config.cpp
    src/Keyboard/PhysicalKeys.cpp
    src/Keyboard/ToKeys/CharToKeys.cpp
    src/Keyboard/ToKeys/CommandToKeys.cpp
    src/Keyboard/ToKeys/SequenceToKeys.cpp
    src/Keyboard/ToKeys/MovementToKeys.cpp
    src/Keyboard/ToKeys/MovementToKeysPrimitives.cpp
    src/Keyboard/ToKeys/EditToKeys.cpp
    src/Keyboard/ToKeys/EditToKeysPrimitives.cpp
    src/Keyboard/ToKeys/CountToKeys.cpp
    src/Types/CharInterval.cpp
  )
  target_include_directories(vimfy_planner_wasm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
  )
  target_compile_definitions(vimfy_planner_wasm PRIVATE
    $<$<BOOL:${VIMF_LEGACY_VIM}>:VIMF_LEGACY_VIM>
  )
  set_target_properties(vimfy_planner_wasm PROPERTIES
    OUTPUT_NAME vimfy_planner
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/wasm
    POSITION_INDEPENDENT_CODE OFF
  )
  # No exception catching: the wrapper sanitizes input so the one throwing
  # call in the closure (CHAR_TO_KEYS.at) cannot fire.
  target_link_options(vimfy_planner_wasm PRIVATE
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sENVIRONMENT=web
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=33554432
    -sASSERTIONS=0
    --no-entry
    -sEXPORTED_RUNTIME_METHODS=cwrap,UTF8ToString,stringToNewUTF8
    -sEXPORTED_FUNCTIONS=_malloc,_free,_vf_plan,_vf_grid,_vf_tiling,_vf_seal
  )
  return()
endif()

# fast_float: portable, header-only replacement for std::from_chars<double>.
# Apple's libc++ ships from_chars<double> as = delete (P0067 unimplemented as
# of Xcode 16), so std::from_chars<double> fails to compile on macOS.
# fast_float is faster than std::from_chars even where the latter works.
include(FetchContent)
FetchContent_Declare(
    fast_float
    GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
    GIT_TAG        v8.0.0
)
FetchContent_MakeAvailable(fast_float)

# Source globs are intentional for fast-moving local development.
file(GLOB_RECURSE VIMFY_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
list(FILTER VIMFY_CORE_SOURCES EXCLUDE REGEX [[/src/(LuaExports/|Wasm/|lua_exports\.cpp$|main\.cpp$)]])

add_library(vimfy_core STATIC ${VIMFY_CORE_SOURCES})
target_link_libraries(vimfy_core PUBLIC fast_float)

# We can reference everything under src/ absolutely. The generated/ directory
# carries BuildConfig.h; source includes it as `#include "BuildConfig.h"`.
target_include_directories(vimfy_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# VIMF_TRACK_STATES is consumed via the generated BuildConfig.h (see above).
# VIMF_DEBUG and VIMF_LEGACY_VIM are still surfaced as raw macros here because
# their source-side readers currently use mismatched names (tracked as
# cleanup items in dev/architecture/todo.md).
target_compile_definitions(vimfy_core PUBLIC
    $<$<BOOL:${VIMF_DEBUG}>:VIMF_DEBUG>
    $<$<BOOL:${VIMF_LEGACY_VIM}>:VIMF_LEGACY_VIM>
)

# SHARED library for Neovim/Lua direct usage
file(GLOB VIMFY_LUA_EXPORT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LuaExports/*.cpp
)

add_library(vimficiency SHARED
    src/lua_exports.cpp
    ${VIMFY_LUA_EXPORT_SOURCES}
)
target_link_libraries(vimficiency PRIVATE vimfy_core)

# CLI for independent testing
add_executable(vimficiency_cli src/main.cpp)
target_link_libraries(vimficiency_cli PRIVATE vimfy_core)

if(VIMF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  # Tooling (e.g. exploration data collector; depends on test_utils)
  add_subdirectory(tools)
endif()
