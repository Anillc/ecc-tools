add_subdirectory(${ICTS_UTILS}/tree_builder/bound_skew_tree)
add_subdirectory(${ICTS_UTILS}/tree_builder/concurrent_bst_salt)
add_subdirectory(${ICTS_UTILS}/tree_builder/local_legalization)

add_library(icts_tree_builder INTERFACE)

target_link_libraries(icts_tree_builder INTERFACE icts_utils_algorithm_core)

target_include_directories(icts_tree_builder INTERFACE ${ICTS_UTILS}/tree_builder)
