add_library(icts_source_flow_run_setup ${ICTS_FLOW_RUN_SETUP}/CTSRunSetup.cc)

target_link_libraries(
  icts_source_flow_run_setup
  PRIVATE
  idm
  icts_source_database
  icts_source_utils
)

target_include_directories(
  icts_source_flow_run_setup
  PUBLIC
  ${ICTS_FLOW}
)

if(DEBUG_ICTS_SOURCE_FLOW_RUN_SETUP)
  if(MSVC)
    target_compile_options(icts_source_flow_run_setup PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow_run_setup PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow_run_setup PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
