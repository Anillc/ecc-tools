add_library(icts_source_flow_setup ${ICTS_FLOW_SETUP}/Setup.cc)

target_link_libraries(
  icts_source_flow_setup
  PRIVATE
  idm
  icts_source_database
  icts_source_utils
)

target_include_directories(
  icts_source_flow_setup
  PUBLIC
  ${ICTS_FLOW}
)
