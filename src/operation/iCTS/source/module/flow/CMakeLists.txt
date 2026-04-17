add_library(icts_flow ${ICTS_MODULE}/flow/CTSFlow.cc)

target_link_libraries(icts_flow PRIVATE icts_data_manager
                                        icts_session
                                        icts_evaluator
                                        icts_router
                                        icts_timing_propagator
                                        icts_math
                                        icts_source_external_libs
                                        icts_api_external_libs
                                        usage
                                        feature_db)

target_include_directories(icts_flow PUBLIC ${ICTS_MODULE}/flow PRIVATE ${ICTS_API})
