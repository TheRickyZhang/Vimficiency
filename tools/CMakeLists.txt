# =============================================================================
# Exploration collector binary
# =============================================================================
# Collects A* explored states for dashboard visualization.
# Run with: VIMFY_SEED_MODE=fixed ./vimfy_explore
#
# Only configured when VIMF_TRACK_STATES=ON: the binary's whole job is to
# emit explored-state traces, and without tracking it trips a static_assert
# in ExplorationCollector.cpp. Gating the target here (rather than letting
# the assert fire) keeps the default `cmake --build build` clean.
if(VIMF_TRACK_STATES)
  file(GLOB VIMFY_EXPLORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Exploration/*.cpp
  )

  add_executable(vimfy_explore
    ${VIMFY_EXPLORE_SOURCES}
  )

  target_include_directories(vimfy_explore
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
  )

  target_link_libraries(vimfy_explore
    PRIVATE
      test_utils
  )
endif()
