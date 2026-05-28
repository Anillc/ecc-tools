add_library(icts_source_flow_instantiation_idb_conversion ${ICTS_FLOW_INSTANTIATION_IDB_CONVERSION}/IdbConversion.cc)

target_link_libraries(
  icts_source_flow_instantiation_idb_conversion
  PRIVATE
  icts_source_database_adapter_sta
  icts_source_database_design
  icts_source_database_io
  icts_source_utils_logger
  log
)

target_include_directories(
  icts_source_flow_instantiation_idb_conversion
  PUBLIC
  ${ICTS_FLOW}
)
