add_library(eval_data
    ${EVAL_DATA}/CongBin.cpp
    ${EVAL_DATA}/TimingNet.cpp
    ${EVAL_DATA}/WLNet.cpp
)

set_target_properties(eval_data PROPERTIES LINK_INTERFACE_MULTIPLICITY 3)
get_property(LIM_COUNT TARGET eval_data PROPERTY "LINK_INTERFACE_MULTIPLICITY")
message("LINK_INTERFACE_MULTIPLICITY: ${LIM_COUNT}")

target_link_libraries(eval_data
    PUBLIC
        eval_util
        eval_source_external_libs
        eval_api_external_libs
)

target_include_directories(eval_data
    PUBLIC
        ${EVAL_DATA}
)

