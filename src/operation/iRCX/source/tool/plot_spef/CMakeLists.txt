add_library(ircx_tool_plot_spef
  PlotSpefTool.cc
  config/PlotSpefConfig.cc
  gds/PlotSpefGdsWriter.cc
  lyp/PlotSpefLypWriter.cc
)

target_link_libraries(ircx_tool_plot_spef
  PUBLIC
    ircx_headers
  PRIVATE
    gdsii-parser
    log
    spef
)

target_include_directories(ircx_tool_plot_spef
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
