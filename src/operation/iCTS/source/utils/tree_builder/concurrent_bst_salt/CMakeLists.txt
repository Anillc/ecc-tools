add_library(icts_cbs INTERFACE)

target_link_libraries(icts_cbs INTERFACE icts_utils_algorithm_core)

target_include_directories(icts_cbs INTERFACE ${ICTS_UTILS}/tree_builder/concurrent_bst_salt)
