# add_subdirectory(${HOME_THIRDPARTY}/pybind11 binary_dir)


SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
# DEFINE PYTHON MODULE & CMAKE TARGET
include_directories(${PYBIND11_PATH}/include)
include_directories(${TCL_PATH})
set(MtKahypar_PATH ${AI_EDA_TOP_DIR}/third_party/iEDA/src/third_party/mt-kahypar/build/lib/)
message(STATUS "pybind11 path: ${PYBIND11_PATH}/include")
message(STATUS "build python lib: ieda_py")
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH}:${MtKahypar_PATH}")
# set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
# set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
pybind11_add_module(ieda_py)

add_subdirectory(py_idb)
add_subdirectory(py_config)
add_subdirectory(py_flow)
add_subdirectory(py_icts)
add_subdirectory(py_idrc)
add_subdirectory(py_ifp)
add_subdirectory(py_ino)
add_subdirectory(py_instance)
add_subdirectory(py_ipdn)
add_subdirectory(py_ipl)
add_subdirectory(py_irt)
add_subdirectory(py_ista)
add_subdirectory(py_ipw)
add_subdirectory(py_ito)
add_subdirectory(py_report)
add_subdirectory(py_feature)
add_subdirectory(py_eval)
add_subdirectory(py_imp)
# ADD SOURCE FILES 
#file(GLOB_RECURSE PYTHON_INTERFACE_SRC CONFIGURE_DEPENDS ./*.cpp ./*.cc)
# file(GLOB PYTHON_INTERFACE_SRC CONFIGURE_DEPENDS *.cpp)

# SET SOURCE FILES
target_sources(ieda_py PUBLIC python_moodule.cc)

# 使用以下命令导出一个静态库目标的所有依赖库

# add_executable(ieda_py_test test1.cc )

# target_link_libraries(ieda_py_test
#     PRIVATE
#     # -Wl,--start-group
#     # py_idb
#     # py_config
#     # py_flow
#     # py_icts
#     # py_idrc
#     py_ipl
#     # "$<LINK_GROUP:RESCAN,py_ipl>" # this is the magic
#     # tool_manager
#     # ipl-api
#     # pybind11::pybind11
#     # py_ipl
#     # py_ifp
#     # py_ino
#     # py_inst
#     # py_ipdn
#     # py_irt
#     # py_ista
#     # py_ipw
#     # py_ito
#     # py_report
#     # py_feature
#     # py_eval
#     # log
#     # -Wl,--end-group
# )
# set_target_properties(ieda_py_test
#     PROPERTIES
#     LINK_OPTIONS "-Wl,--start-group"
#     )

# target_link_libraries(ieda_py_test
#     PRIVATE
#         py_idb
#         py_config
#         py_flow
#         py_icts
#         py_idrc
#         py_ifp
#         py_ino
#         py_inst
#         py_ipdn
#         py_ipl
#         py_irt
#         py_ista
#         py_ipw
#         py_ito
#         py_report
#         py_feature
#         py_eval
#         log
# )

target_link_libraries(ieda_py
    PRIVATE
        py_idb
        py_config
        py_flow
        py_icts
        py_idrc
        py_ifp
        py_ino
        py_inst
        py_ipdn
        py_ipl
        py_irt
        py_ista
        py_ipw
        py_ito
        py_report
        py_feature
        py_eval
        log
        py_imp

        -u_ZTVN3ipl21ElectricFieldGradientE
        -u_ZN3ipl8Nesterov17correctNextCoordiEiNS_5PointIiEE
        -u_ZN3ipl21ElectricFieldGradient17initElectro2DListEv
        -u_ZN3ipl8Nesterov23calculateNextSteplengthESt6vectorINS_5PointIfEESaIS3_EE
        -u_ZN3ipl8Nesterov11runNextIterEii
        -u_ZN3ipl8Nesterov16runBackTrackIterEi
        -u_ZN3ipl8Nesterov12initNesterovESt6vectorINS_5PointIiEESaIS3_EES1_INS2_IfEESaIS6_EES5_S8_
        -u_ZN3ipl8Nesterov20correctNextSLPCoordiEiNS_5PointIiEE
        -u_ZN3ipl20WAWirelengthGradientC1EPNS_15TopologyManagerE
)

install(TARGETS ieda_py
 DESTINATION ${CMAKE_INSTALL_PREFIX}
    )

get_target_property(DEPENDENCIES ieda_py INTERFACE_LINK_LIBRARIES)
message(STATUS "ieda_py install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "ieda_py dependencies: ${INTERFACE_LINK_LIBRARIES}")