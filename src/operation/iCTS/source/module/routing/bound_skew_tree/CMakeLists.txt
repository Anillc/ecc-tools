add_library(
  icts_source_module_routing_bst
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/BSTRouter.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/clock_tree_conversion/BSTRouterBinaryTopology.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/clock_tree_conversion/BSTRouterExport.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/component/Components.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/geometry/GeomCalc.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/geometry/GeomCalcLine.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/geometry/GeomCalcPointRegion.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/geometry/GeomCalcTransformedRect.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/BoundSkewTree.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/BoundSkewTreeBalance.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/BoundSkewTreeEmbedding.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/BoundSkewTreeFlow.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/BoundSkewTreeInfeasibleMerge.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/BoundSkewTreeJoining.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/BoundSkewTreeTopology.cc
)

target_link_libraries(
  icts_source_module_routing_bst
  PUBLIC
  icts_source_database
  PRIVATE
  icts_source_database_routing
  icts_source_utils
  log
)

target_include_directories(
  icts_source_module_routing_bst
  PUBLIC
  ${ICTS_MODULE_ROUTING}
  PRIVATE
  ${ICTS_DATABASE}
  ${ICTS_UTILS}
)

if(DEBUG_ICTS_SOURCE_MODULE_ROUTING_BST)
  if(MSVC)
    target_compile_options(icts_source_module_routing_bst PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_module_routing_bst PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_module_routing_bst PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
