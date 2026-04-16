if(DEBUG_ICTS_COMMITTER)
  message(STATUS "CTS: DEBUG_ICTS_COMMITTER")
  set(CMAKE_BUILD_TYPE "Debug")
else()
  message(STATUS "CTS: RELEASE_ICTS_COMMITTER")
  set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(icts_committer ${ICTS_MODULE}/committer/DesignCommitter.cc)

target_link_libraries(icts_committer PUBLIC icts_synthesis_ir PRIVATE icts_data_manager icts_timing_propagator)

target_include_directories(icts_committer PUBLIC ${ICTS_MODULE}/committer)
