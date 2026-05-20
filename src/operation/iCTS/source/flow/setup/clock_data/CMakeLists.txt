add_library(icts_source_flow_setup_clock_data ${ICTS_FLOW_SETUP_CLOCK_DATA}/ClockDataRead.cc)

target_link_libraries(
  icts_source_flow_setup_clock_data
  PUBLIC
  icts_source_database
  PRIVATE
  icts_source_utils
)

target_include_directories(
  icts_source_flow_setup_clock_data
  PUBLIC
  ${ICTS_FLOW}
)
