add_library(
  icts_source_flow_synthesis_distribution
  ${ICTS_FLOW_SYNTHESIS_DISTRIBUTION}/ClockDistribution.cc
)

target_link_libraries(
  icts_source_flow_synthesis_distribution
  PUBLIC
  icts_source_flow_synthesis_trace_layout
  PRIVATE
  icts_source_database_adapter_sta
  icts_source_flow_instantiation_design_conversion
  icts_source_flow_synthesis_trace_domain_status
  log
)

target_include_directories(
  icts_source_flow_synthesis_distribution
  PUBLIC
  ${ICTS_FLOW}
)
