set script_dir [file dirname [file normalize [info script]]]

read_liberty "$script_dir/cts_fast_sta_alignment.lib"
read_verilog "$script_dir/cts_fast_sta_alignment.v"
link_design top

create_clock -name clk -period 10 [get_ports clk]
set_input_delay -clock clk 0 [get_ports clk]
set_output_delay -clock clk 0 [get_ports sink]
set_input_transition 0.2 [get_ports clk]
set_propagated_clock [get_clocks clk]
set_delay_calculator dmp_ceff_elmore

# CTS-like manual Pi/Elmore parasitics.  These values are intentionally small
# and deterministic so fast STA can run the same case with owned data.
sta::set_pi_model u_buf/Y 0.2 1.0 0.8
sta::set_elmore u_buf/Y u_leaf/A 0.15
sta::set_pi_model u_leaf/Y 0.1 1.0 0.4
sta::set_elmore u_leaf/Y sink 0.08

puts "--- report_dcalc u_buf ---"
report_dcalc -from [get_pins u_buf/A] -to [get_pins u_buf/Y] -max -digits 6

puts "--- report_dcalc u_leaf ---"
report_dcalc -from [get_pins u_leaf/A] -to [get_pins u_leaf/Y] -max -digits 6

puts "--- report_checks fields ---"
report_checks -path_delay max -fields {slew cap input_pins net fanout} -digits 6

puts "--- report_power json ---"
report_power -format json -digits 8
