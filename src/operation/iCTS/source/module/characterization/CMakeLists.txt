set(ICTS_MODULE_CHAR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/Characterization.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/builder/CharBuilder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/builder/CharBuilderBuild.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/builder/CharBuilderConfig.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/builder/CharBuilderFeasibility.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/builder/CharBuilderTopology.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/circuit/CharBuilderCircuit.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/CharBuilderPatternEnumeration.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern/CharBuilderPatternStorage.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/sampling/CharBuilderSampleStorage.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/sampling/CharBuilderSampling.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/sampling/CharBuilderSlewSampling.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/sampling/CharBuilderStaSampling.cc
)

add_library(icts_source_module_characterization ${ICTS_MODULE_CHAR_SRC})

target_link_libraries(
  icts_source_module_characterization
  PUBLIC
  icts_source_database_characterization
  icts_source_database_routing
  PRIVATE
  icts_source_database_adapter_fast_sta
  icts_source_utils
  icts_source_utils_logger
)

target_include_directories(
  icts_source_module_characterization
  PUBLIC
  ${ICTS_MODULE}
  PRIVATE
  ${ICTS_DATABASE}
  ${ICTS_UTILS}
)

if(DEBUG_ICTS_SOURCE_MODULE_CHARACTERIZATION)
  if(MSVC)
    target_compile_options(icts_source_module_characterization PRIVATE /Zi /Od /Ob0)
  else()
    target_compile_options(icts_source_module_characterization PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
