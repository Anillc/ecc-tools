add_library(
  icts_evaluator_service
  ${ICTS_MODULE}/evaluator/service/TimingAnalysisService.cc
  ${ICTS_MODULE}/evaluator/service/MetricsCollector.cc
  ${ICTS_MODULE}/evaluator/service/StatisticsWriter.cc
  ${ICTS_MODULE}/evaluator/service/DebugPlotService.cc)

target_link_libraries(icts_evaluator_service PUBLIC icts_data_manager icts_report icts_runtime icts_synthesis_ir)

target_include_directories(icts_evaluator_service PUBLIC ${ICTS_MODULE}/evaluator
                                                         ${ICTS_MODULE}/evaluator/service)
