set(IRCX_ENGINE_SRC
  ParasiticXEngine.cpp
  ParasiticXIDBAdapter.cpp
)

add_library(ircx_engine ${IRCX_ENGINE_SRC})

target_link_libraries(ircx_engine
  PUBLIC
    IdbBuilder
    ircx_headers
  PRIVATE
    def_service
    idb
    lef_service
    log
)

target_include_directories(ircx_engine
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
