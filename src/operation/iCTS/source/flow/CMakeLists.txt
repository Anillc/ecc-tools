set(ICTS_FLOW_HTREE ${ICTS_FLOW}/htree)
set(ICTS_FLOW_NUMERICAL_HTREE ${ICTS_FLOW}/numerical_htree)
set(ICTS_FLOW_SYNTHESIS ${ICTS_FLOW}/synthesis)

add_subdirectory(${ICTS_FLOW_HTREE})
add_subdirectory(${ICTS_FLOW_NUMERICAL_HTREE})
add_subdirectory(${ICTS_FLOW_SYNTHESIS})

add_library(icts_source_flow ${ICTS_FLOW}/FlowManager.cc)

target_link_libraries(
  icts_source_flow
  PRIVATE
  icts_source_module
  icts_source_flow_htree
  icts_source_flow_numerical_htree
  icts_source_flow_synthesis
)

target_include_directories(icts_source_flow PUBLIC ${ICTS_FLOW})

if(DEBUG_ICTS_SOURCE_FLOW)
  if(MSVC)
    target_compile_options(icts_source_flow PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_source_flow PRIVATE /DEBUG)
  else()
    target_compile_options(icts_source_flow PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
