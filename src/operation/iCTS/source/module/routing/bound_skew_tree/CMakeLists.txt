add_library(
  icts_source_module_routing_bst
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/BSTRouter.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/BoundSkewTree.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/BoundSkewTreeBalance.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/BoundSkewTreeEmbedding.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/BoundSkewTreeFlow.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/BoundSkewTreeJoining.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/BoundSkewTreeTopology.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/Components.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/GeomCalc.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/GeomCalcLine.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/GeomCalcPointRegion.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/GeomCalcTransformedRect.cc
)

target_link_libraries(
  icts_source_module_routing_bst
  PUBLIC
  icts_source_database
  PRIVATE
  icts_source_module_routing_database
  icts_source_utils
)

target_include_directories(
  icts_source_module_routing_bst
  PUBLIC
  ${ICTS_MODULE_ROUTING}/bound_skew_tree
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
