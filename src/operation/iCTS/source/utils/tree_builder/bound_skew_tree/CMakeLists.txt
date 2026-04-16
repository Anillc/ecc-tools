add_library(icts_bst INTERFACE)

target_link_libraries(icts_bst INTERFACE icts_utils_algorithm_core)

target_include_directories(icts_bst INTERFACE ${ICTS_UTILS}/tree_builder/bound_skew_tree)
