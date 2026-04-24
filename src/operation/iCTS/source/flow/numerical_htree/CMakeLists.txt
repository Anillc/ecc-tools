add_library(icts_source_flow_numerical_htree ${ICTS_FLOW_NUMERICAL_HTREE} / NumericalHTreeBuilder.cc)

    target_link_libraries(icts_source_flow_numerical_htree PUBLIC icts_source_database PRIVATE icts_source_module
                              icts_source_module_numerical_characterization icts_source_utils)

        target_include_directories(icts_source_flow_numerical_htree PUBLIC ${ICTS_FLOW})
