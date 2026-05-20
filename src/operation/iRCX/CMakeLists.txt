if(TEST_SHELL)
  add_definitions(-DTEST_SHELL)
endif()

# ADD_COMPILE_OPTIONS("-fsanitize=address") LINK_LIBRARIES("-fsanitize=address")

add_library(ircx_headers INTERFACE)

target_compile_features(ircx_headers INTERFACE cxx_std_20)

target_include_directories(ircx_headers
  INTERFACE
    ${HOME_UTILITY}
    ${HOME_UTILITY}/log
    ${HOME_UTILITY}/stdBase
    ${HOME_UTILITY}/stdBase/include
    ${HOME_OPERATION}/iRCX
    ${HOME_OPERATION}/iRCX/api
    ${HOME_OPERATION}/iRCX/source/config
    ${HOME_OPERATION}/iRCX/source/module
    ${HOME_OPERATION}/iRCX/source/module/database
    ${HOME_OPERATION}/iRCX/source/module/environment
    ${HOME_OPERATION}/iRCX/source/module/extract
    ${HOME_OPERATION}/iRCX/source/module/extract/capacitance
    ${HOME_OPERATION}/iRCX/source/module/extract/resistance
    ${HOME_OPERATION}/iRCX/source/module/process
    ${HOME_OPERATION}/iRCX/source/module/report
    ${HOME_OPERATION}/iRCX/source/module/topology
    ${HOME_OPERATION}/iRCX/source/parser
    ${HOME_OPERATION}/iRCX/source/parser/cap_table
    ${HOME_OPERATION}/iRCX/source/parser/itf
    ${HOME_OPERATION}/iRCX/source/parser/itf_builder
    ${HOME_OPERATION}/iRCX/source/parser/itf_data
    ${HOME_OPERATION}/iRCX/source/parser/mapping
)

target_include_directories(ircx_headers
  SYSTEM INTERFACE
    ${HOME_THIRDPARTY}
)

add_subdirectory(source/config)
add_subdirectory(api)
add_subdirectory(source/module/topology)
add_subdirectory(source/module/environment)
add_subdirectory(source/module/process)
add_subdirectory(source/module/extract)
add_subdirectory(source/module/report)
add_subdirectory(source/parser)
