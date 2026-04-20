add_library(ircx_process ProcessVariation.cpp)

target_link_libraries(ircx_process
  PUBLIC
    ircx_headers
  PRIVATE
    ircx_environment
    itf_data
    OpenMP::OpenMP_CXX
    usage
)

target_include_directories(ircx_process
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
