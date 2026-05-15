# =============================================================================
# Exploration collector binary
# =============================================================================
# Collects A* explored states for dashboard visualization.
# Run with: VIMFICIENCY_SEED_MODE=fixed ./vimficiency_explore
#
# Only configured when VIMF_TRACK_STATES=ON: the binary's whole job is to
# emit explored-state traces, and without tracking it trips a static_assert
# in ExplorationCollector.cpp. Gating the target here (rather than letting
# the assert fire) keeps the default `cmake --build build` clean.
if(VIMF_TRACK_STATES)
  add_executable(vimficiency_explore
    Exploration/CompositionCases.cpp
    Exploration/CompositionJsonWriter.cpp
    Exploration/EditCases.cpp
    Exploration/SequenceChunker.cpp
    Exploration/ExplorationCollector.cpp
    Exploration/ExplorationJsonWriter.cpp
    Exploration/MotionCases.cpp
  )

  target_include_directories(vimficiency_explore
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
  )

  target_link_libraries(vimficiency_explore
    PRIVATE
      test_utils
  )
endif()
