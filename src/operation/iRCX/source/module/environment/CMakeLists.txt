add_library(ircx_environment Environment.cpp)

target_link_libraries(ircx_environment
  PUBLIC
    ircx_topology
    log
  PRIVATE
    OpenMP::OpenMP_CXX
)

target_include_directories(ircx_environment
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
