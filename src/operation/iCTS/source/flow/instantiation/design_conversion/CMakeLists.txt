add_library(icts_source_flow_instantiation_design_conversion ${ICTS_FLOW_INSTANTIATION_DESIGN_CONVERSION}/DesignConversion.cc)

target_link_libraries(
  icts_source_flow_instantiation_design_conversion
  PUBLIC
  icts_source_database
  PRIVATE
  icts_source_database_design
  icts_source_utils
)

target_include_directories(
  icts_source_flow_instantiation_design_conversion
  PUBLIC
  ${ICTS_FLOW}
)
