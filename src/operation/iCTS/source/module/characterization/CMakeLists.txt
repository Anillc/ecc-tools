set(ICTS_MODULE_CHAR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderBuild.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderCircuit.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderConfig.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderFeasibility.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderPatternEnumeration.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderPatternStorage.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderSampleStorage.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderSampling.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderSlewSampling.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderStaSampling.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/CharBuilderTopology.cc
)

add_library(icts_source_module_characterization ${ICTS_MODULE_CHAR_SRC})

target_link_libraries(
  icts_source_module_characterization
  PUBLIC
  icts_source_database_characterization
  PRIVATE
  icts_source_database_adapter_sta
  icts_source_database_config
  icts_source_utils
  icts_source_utils_logger
)

target_include_directories(
  icts_source_module_characterization
  PUBLIC
  ${ICTS_MODULE}
  ${ICTS_MODULE_CHARACTERIZATION}
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
