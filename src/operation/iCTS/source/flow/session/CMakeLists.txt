add_library(icts_source_flow_session ${ICTS_FLOW_SESSION}/CTSRunEnvironment.cc)

target_link_libraries(
  icts_source_flow_session
  PRIVATE
  idm
  icts_source_database
  icts_source_utils
)

target_include_directories(
  icts_source_flow_session
  PUBLIC
  ${ICTS_FLOW}
)

if(DEBUG_ICTS_SOURCE_FLOW_SESSION)
  if(MSVC)
    target_compile_options(icts_source_flow_session PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow_session PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow_session PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
