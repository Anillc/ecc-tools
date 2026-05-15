add_library(icts_source_flow_instantiation_idb_conversion ${ICTS_FLOW_INSTANTIATION_IDB_CONVERSION}/IdbConversion.cc)

target_link_libraries(
  icts_source_flow_instantiation_idb_conversion
  PRIVATE
  icts_source_database
  icts_source_utils
)

target_include_directories(
  icts_source_flow_instantiation_idb_conversion
  PUBLIC
  ${ICTS_FLOW}
)
