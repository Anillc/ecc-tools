add_library(
  icts_synthesis_operator
  ${ICTS_UTILS}/synthesis_operator/SolverNetBuilder.cc
  ${ICTS_UTILS}/synthesis_operator/SinkClusteringOperator.cc
  ${ICTS_UTILS}/synthesis_operator/TopologyBuilderOperator.cc
  ${ICTS_UTILS}/synthesis_operator/LongWireBufferingOperator.cc
  ${ICTS_UTILS}/synthesis_operator/LevelSizingOperator.cc
  ${ICTS_UTILS}/synthesis_operator/NetSynthesisPipeline.cc)

target_link_libraries(icts_synthesis_operator PUBLIC icts_synthesis_ir
                                                     icts_timing_propagator
                                                     icts_tree_builder
                                                     icts_balance_clustering)

target_include_directories(icts_synthesis_operator PUBLIC ${ICTS_UTILS}/synthesis_operator)
