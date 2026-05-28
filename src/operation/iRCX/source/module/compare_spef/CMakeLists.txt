add_library(ircx_compare_spef
  Comparator.cc
  CouplingCapComparator.cc
  CompareMath.cc
  CompareSpefConfig.cc
  NetSelector.cc
  PathPairGenerator.cc
  ReportWriter.cc
  ResistanceSolver.cc
  ResultSorter.cc
  SpefReader.cc
)

target_link_libraries(ircx_compare_spef
  PUBLIC
    ircx_headers
  PRIVATE
    log
    spef
    fort
)

target_include_directories(ircx_compare_spef
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
