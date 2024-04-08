set work_dir "/home/taosimin/nangate45/ista" 

set_design_workspace /home/longshuaiying/verilog_parser_rust/nangate45


# read_netlist $work_dir/../design/example/example1.v
# read_netlist /home/longshuaiying/iEDA/src/database/manager/parser/verilog/verilog-rust/verilog-parser/example/example1.v

set LIB_FILES $work_dir/../lib/NangateOpenCellLibrary_typical.lib
#/home/taosimin/nangate45/lib/NangateOpenCellLibrary_typical.lib

read_liberty $LIB_FILES

# link_design top

# read_sdc  $work_dir/../design/example/example1.sdc
# read_spef $work_dir/../design/example/example1.spef

# report_timing
