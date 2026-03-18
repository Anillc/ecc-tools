add_subdirectory(${ICTS_MODULE_ROUTING}/flute)
add_subdirectory(${ICTS_MODULE_ROUTING}/salt)
add_subdirectory(${ICTS_MODULE_ROUTING}/bound_skew_tree)
add_subdirectory(${ICTS_MODULE_ROUTING}/concurrent_bst_salt)

add_library(
  icts_source_module_routing
  ${ICTS_MODULE_ROUTING}/Router.cc
  ${ICTS_MODULE_ROUTING}/local_legalization/LocalLegalization.cc
)

target_link_libraries(
  icts_source_module_routing
  PUBLIC
  icts_source_database
  icts_source_module_routing_flute
  icts_source_module_routing_bst
  icts_source_module_routing_cbs
  icts_source_module_routing_salt
  icts_source_utils
  lemon
)

target_include_directories(
  icts_source_module_routing
  PUBLIC
  ${ICTS_MODULE_ROUTING}
  PRIVATE
  ${ICTS_DATABASE}
  ${ICTS_UTILS}
)

if(DEBUG_ICTS_SOURCE_MODULE_ROUTING)
  if(MSVC)
    target_compile_options(icts_source_module_routing PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_module_routing PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_module_routing PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
