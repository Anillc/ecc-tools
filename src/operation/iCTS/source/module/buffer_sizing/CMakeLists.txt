add_library(
  icts_source_module_buffer_sizing
  ${ICTS_MODULE_BUFFER_SIZING}/CharTimingLookup.cc
  ${ICTS_MODULE_BUFFER_SIZING}/TreeBufferSizingBranch.cc
  ${ICTS_MODULE_BUFFER_SIZING}/TreeBufferSizingEvaluate.cc
  ${ICTS_MODULE_BUFFER_SIZING}/TreeBufferSizingSolve.cc
)

target_link_libraries(
  icts_source_module_buffer_sizing
  PUBLIC
  icts_source_database_characterization
  PRIVATE
  icts_source_module_characterization
  icts_source_utils_graph
  icts_source_utils_logger
)

target_include_directories(
  icts_source_module_buffer_sizing
  PUBLIC
  ${ICTS_MODULE_BUFFER_SIZING}
  ${ICTS_MODULE}
)

if(DEBUG_ICTS_SOURCE_MODULE_BUFFER_SIZING)
  if(MSVC)
    target_compile_options(icts_source_module_buffer_sizing PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_module_buffer_sizing PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_module_buffer_sizing PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
