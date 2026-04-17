add_library(icts_evaluator_context ${ICTS_MODULE}/evaluator/context/RuntimeContextAdapter.cc)

target_link_libraries(icts_evaluator_context PUBLIC icts_runtime PRIVATE icts_data_manager icts_synthesis_ir)

target_include_directories(icts_evaluator_context PUBLIC ${ICTS_MODULE}/evaluator ${ICTS_MODULE}/evaluator/context)
