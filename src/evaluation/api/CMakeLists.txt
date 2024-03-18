set(DEBUG_EVAL_API ON)
# if(DEBUG_EVAL_API)
#     message(STATUS "evaluator: DEBUG EVAL_API")
#     set(CMAKE_BUILD_TYPE "Debug")
# else()
#     message(STATUS "evaluator: RELEASE EVAL_API")
#     set(CMAKE_BUILD_TYPE "Release")
# endif()

add_library(eval_api
    ${EVAL_API}/EvalAPI.cpp
)

target_link_libraries(eval_api
    PUBLIC
        eval_wirelength
        eval_module
        irt_api     
    INTERFACE
        eval_congestion
        eval_timing
        eval_wirelength
)

target_include_directories(eval_api
    PUBLIC
        ${EVAL_API}
        ${EVAL_API}../source/
)
