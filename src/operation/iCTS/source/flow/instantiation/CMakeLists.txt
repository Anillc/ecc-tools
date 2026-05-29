set(ICTS_FLOW_INSTANTIATION_DESIGN_CONVERSION ${ICTS_FLOW_INSTANTIATION}/design_conversion)
set(ICTS_FLOW_INSTANTIATION_IDB_CONVERSION ${ICTS_FLOW_INSTANTIATION}/idb_conversion)

add_subdirectory(${ICTS_FLOW_INSTANTIATION_DESIGN_CONVERSION})

add_library(
  icts_source_flow_instantiation
  ${ICTS_FLOW_INSTANTIATION}/Instantiation.cc
  ${ICTS_FLOW_INSTANTIATION_IDB_CONVERSION}/IdbConversion.cc
)

target_link_libraries(
  icts_source_flow_instantiation
  PRIVATE
  icts_source_flow_instantiation_design_conversion
  icts_source_database_design
  icts_source_database_io
  icts_source_utils_logger
  log
)

target_include_directories(
  icts_source_flow_instantiation
  PUBLIC
  ${ICTS_FLOW}
)
