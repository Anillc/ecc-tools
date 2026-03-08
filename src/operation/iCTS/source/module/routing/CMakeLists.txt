add_library(
  icts_source_module_routing
  ${ICTS_MODULE_ROUTING}/Router.cc
  ${ICTS_MODULE_ROUTING}/flute/FLUTE.cc
  ${ICTS_MODULE_ROUTING}/salt/SALT.cc
  ${ICTS_MODULE_ROUTING}/legacy_module/Inst.cc
  ${ICTS_MODULE_ROUTING}/legacy_module/Pin.cc
  ${ICTS_MODULE_ROUTING}/legacy_module/Net.cc
  ${ICTS_MODULE_ROUTING}/legacy_module/TreeBuilder.cc
  ${ICTS_MODULE_ROUTING}/timing/TimingPropagator.cc
  ${ICTS_MODULE_ROUTING}/local_legalization/LocalLegalization.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/GeomCalc.cc
  ${ICTS_MODULE_ROUTING}/bound_skew_tree/BoundSkewTree.cc
  ${ICTS_MODULE_ROUTING}/concurrent_bst_salt/CBS.cc
)

target_link_libraries(
  icts_source_module_routing
  PUBLIC
  icts_source_database
  icts_source_utils
  PRIVATE
  salt
  flute
)

target_include_directories(
  icts_source_module_routing
  PUBLIC
  ${ICTS_MODULE_ROUTING}
  PRIVATE
  ${ICTS_SOURCE}
  ${ICTS_API}
  ${ICTS_MODULE_ROUTING}/bound_skew_tree
  ${ICTS_MODULE_ROUTING}/concurrent_bst_salt
  ${ICTS_MODULE_ROUTING}/flute
  ${ICTS_MODULE_ROUTING}/salt
  ${ICTS_MODULE_ROUTING}/local_legalization
  ${ICTS_MODULE_ROUTING}/timing
  ${ICTS_MODULE_ROUTING}/legacy_module
)

if(DEBUG_ICTS_SOURCE_MODULE_ROUTING)
  if(MSVC)
    target_compile_options(icts_source_module_routing PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_module_routing PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_module_routing PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
