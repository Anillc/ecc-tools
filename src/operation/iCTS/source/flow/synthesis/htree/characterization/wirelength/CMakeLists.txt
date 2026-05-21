add_library(
  icts_source_flow_synthesis_htree_characterization_wirelength
  ${ICTS_FLOW_SYNTHESIS_HTREE_CHARACTERIZATION_WIRELENGTH}/WirelengthGrid.cc
)

target_link_libraries(
  icts_source_flow_synthesis_htree_characterization_wirelength
  PUBLIC
  icts_source_module
  PRIVATE
  icts_source_database
  icts_source_database_spatial
  icts_source_utils
  icts_source_utils_geometry
)

target_include_directories(
  icts_source_flow_synthesis_htree_characterization_wirelength
  PUBLIC
  ${ICTS_FLOW}
)
