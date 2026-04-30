add_library(
  icts_source_flow_stage
  ${ICTS_FLOW_STAGE}/CTSClockDataLoadStep.cc
  ${ICTS_FLOW_STAGE}/CTSClockTreeEvaluationStep.cc
  ${ICTS_FLOW_STAGE}/CTSClockTreeReportStep.cc
  ${ICTS_FLOW_STAGE}/CTSClockTreeWritebackStep.cc
  ${ICTS_FLOW_STAGE}/ClockSinkDomainBuilder.cc
  ${ICTS_FLOW_STAGE}/ClockTreeSynthesisDriver.cc
  ${ICTS_FLOW_STAGE}/ClockTreeSynthesisStatusTable.cc
  ${ICTS_FLOW_STAGE}/ClockTreeSynthesisTransaction.cc
  ${ICTS_FLOW_STAGE}/CTSClockTreeSynthesisStep.cc
)

target_link_libraries(
  icts_source_flow_stage
  PUBLIC
  icts_source_flow_synthesis
  icts_source_utils
  PRIVATE
  icts_source_database
  icts_source_module
  icts_source_flow_evaluation
  icts_source_flow_netlist
  icts_source_flow_report
)

target_include_directories(
  icts_source_flow_stage
  PUBLIC
  ${ICTS_FLOW}
)

if(DEBUG_ICTS_SOURCE_FLOW_STAGE)
  if(MSVC)
    target_compile_options(icts_source_flow_stage PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow_stage PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow_stage PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
