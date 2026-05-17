# Usage:
#   ecc -script steps/route.tcl gcd

set step_name "route"
set script_dir [file normalize [file dirname [info script]]]
source [file normalize [file join $script_dir step_common.tcl]]

set flow_dir [file normalize [file join $script_dir ..]]
set default_workspace [file normalize [file join $flow_dir gcd home]]
set default_pdk [file normalize [file join $flow_dir .. .. .. .. icsprout55-pdk]]
lassign [step_setup_workspace $default_workspace $default_pdk] workspace_root pdk_root
set step_dir [file join $workspace_root route_ecc]

set design_name "gcd"
set top_module "gcd"

set tech_lef [file join $pdk_root prtech techLEF N551P6M_ecos.lef]
set lef_files [list \
  [file join $pdk_root IP STD_cell ics55_LLSC_H7C_V1p10C100 ics55_LLSC_H7CR lef ics55_LLSC_H7CR_ecos.lef] \
  [file join $pdk_root IP STD_cell ics55_LLSC_H7C_V1p10C100 ics55_LLSC_H7CL lef ics55_LLSC_H7CL_ecos.lef]]
set lib_files [list \
  [file join $pdk_root IP STD_cell ics55_LLSC_H7C_V1p10C100 ics55_LLSC_H7CR liberty ics55_LLSC_H7CR_ss_rcworst_1p08_125_nldm.lib] \
  [file join $pdk_root IP STD_cell ics55_LLSC_H7C_V1p10C100 ics55_LLSC_H7CL liberty ics55_LLSC_H7CL_ss_rcworst_1p08_125_nldm.lib]]

set flow_config [file join $step_dir config flow_config.json]
set db_config [file join $step_dir config db_default_config.json]
set route_config [file join $step_dir config rt_default_config.json]
set route_work_dir [file join $step_dir data rt]
set output_dir [file join $step_dir output]

set input_def [file join $workspace_root legalization_ecc output gcd_legalization.def.gz]
set input_verilog [file join $workspace_root legalization_ecc output gcd_legalization.v]

set output_def [file join $output_dir gcd_route.def.gz]
set output_verilog [file join $output_dir gcd_route.v]
set output_gds [file join $output_dir gcd_route.gds]
set output_json [file join $output_dir gcd_route.json]
set output_db [file join $output_dir gcd_route_db]
set feature_db [file join $step_dir feature route.db.json]
set feature_step [file join $step_dir feature route.step.json]
set report_db [file join $step_dir report route.db.rpt]
set sta_dir [file join $step_dir data sta]

set bottom_routing_layer "MET2"
set top_routing_layer "MET5"
set route_thread_number 50

step_prepare_configs [list $flow_config $db_config $route_config] $workspace_root $pdk_root

puts "=============================="
puts "Running $step_name"
puts "Workspace: $workspace_root"
puts "PDK: $pdk_root"
step_print_list "lib_files" $lib_files
step_print_path "route_config" $route_config
step_print_path "route_work_dir" $route_work_dir

step_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $input_def $input_verilog $top_module
file mkdir $route_work_dir
init_notification
init_rt \
  -temp_directory_path $route_work_dir \
  -bottom_routing_layer $bottom_routing_layer \
  -top_routing_layer $top_routing_layer \
  -thread_number $route_thread_number \
  -output_inter_result 0 \
  -enable_notification 0 \
  -enable_timing 0

run_rt
destroy_rt
step_save_design $step_name $output_def $output_verilog $output_gds $output_json $output_db $feature_db $feature_step $report_db $sta_dir
step_maybe_flow_exit
