add_library(
  icts_source_flow_synthesis
  ${ICTS_FLOW_SYNTHESIS}/ClockSynthesis.cc
)

target_link_libraries(
  icts_source_flow_synthesis
  PUBLIC
  icts_source_database
  icts_source_module
  icts_source_flow_htree
  PRIVATE
  icts_source_utils
)

target_include_directories(
  icts_source_flow_synthesis
  PUBLIC
  ${ICTS_FLOW}
)
