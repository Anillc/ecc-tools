add_subdirectory(${ICTS_MODULE}/runtime)
add_subdirectory(${ICTS_MODULE}/session)
add_subdirectory(${ICTS_MODULE}/evaluator)
add_subdirectory(${ICTS_MODULE}/committer)
add_subdirectory(${ICTS_MODULE}/synthesis)
add_subdirectory(${ICTS_MODULE}/router)
add_subdirectory(${ICTS_MODULE}/flow)

add_library(icts_module INTERFACE)

target_link_libraries(
  icts_module
  INTERFACE icts_runtime
            icts_session
            icts_evaluator
            icts_committer
            icts_synthesis
            icts_router
            icts_flow)
