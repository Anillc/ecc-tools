add_library(ircx_ics55 SHARED IMPORTED GLOBAL)

set_target_properties(ircx_ics55
  PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib/libircx_ics55.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/include"
)

