set(ICTS_UTILS_MATH ${ICTS_UTILS}/math)
set(ICTS_UTILS_SYNTHESIS_IR ${ICTS_UTILS}/synthesis_ir)
set(ICTS_UTILS_SYNTHESIS_OPERATOR ${ICTS_UTILS}/synthesis_operator)
set(ICTS_UTILS_TREE_BUILDER ${ICTS_UTILS}/tree_builder)
set(ICTS_UTILS_TIMING_PROPAGATOR ${ICTS_UTILS}/timing_propagator)
set(ICTS_UTILS_BALANCE_CLUSTERING ${ICTS_UTILS}/balance_clustering)

add_subdirectory(${ICTS_UTILS_MATH})
add_subdirectory(${ICTS_UTILS_SYNTHESIS_IR})

add_library(
  icts_utils_algorithm_core
  ${ICTS_UTILS}/timing_propagator/TimingPropagator.cc
  ${ICTS_UTILS}/tree_builder/TreeBuilder.cc
  ${ICTS_UTILS}/tree_builder/bound_skew_tree/BoundSkewTree.cc
  ${ICTS_UTILS}/tree_builder/bound_skew_tree/GeomCalc.cc
  ${ICTS_UTILS}/tree_builder/concurrent_bst_salt/CBS.cc
  ${ICTS_UTILS}/tree_builder/local_legalization/LocalLegalization.cc
  ${ICTS_UTILS}/balance_clustering/BalanceClustering.cc
  ${ICTS_UTILS}/balance_clustering/anneal_opt/AnnealOpt.cc)

target_link_libraries(icts_utils_algorithm_core PUBLIC icts_data_manager icts_synthesis_ir PRIVATE icts_runtime lemon salt)

target_include_directories(
  icts_utils_algorithm_core
  PUBLIC ${ICTS_DATA_MANAGER}/config
         ${ICTS_DATA_MANAGER}/database
         ${ICTS_UTILS}/synthesis_ir
         ${ICTS_UTILS}/timing_propagator
         ${ICTS_UTILS}/tree_builder
         ${ICTS_UTILS}/tree_builder/bound_skew_tree
         ${ICTS_UTILS}/tree_builder/concurrent_bst_salt
         ${ICTS_UTILS}/tree_builder/local_legalization
         ${ICTS_UTILS}/balance_clustering
         ${ICTS_UTILS}/balance_clustering/anneal_opt
         ${ICTS_UTILS}/balance_clustering/min_cost_flow
         ${PROJECT_SOURCE_DIR}/src/third_party/salt)

add_subdirectory(${ICTS_UTILS_TREE_BUILDER})
add_subdirectory(${ICTS_UTILS_TIMING_PROPAGATOR})
add_subdirectory(${ICTS_UTILS_BALANCE_CLUSTERING})
add_subdirectory(${ICTS_UTILS_SYNTHESIS_OPERATOR})

add_library(icts_tools INTERFACE)
target_link_libraries(icts_tools INTERFACE icts_timing_propagator icts_tree_builder icts_balance_clustering)

add_library(icts_utils INTERFACE)
target_link_libraries(icts_utils INTERFACE icts_math icts_synthesis_ir icts_synthesis_operator icts_tools)
