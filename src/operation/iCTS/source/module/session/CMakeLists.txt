add_library(icts_session ${ICTS_MODULE}/session/CTSSession.cc)

target_link_libraries(icts_session PUBLIC icts_runtime icts_committer icts_synthesis_operator icts_evaluator_context
                                   PRIVATE icts_data_manager icts_api_external_libs)

target_include_directories(icts_session PUBLIC ${ICTS_MODULE}/session PRIVATE ${ICTS_API})
