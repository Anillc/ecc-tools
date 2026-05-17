# Tcl implementation for the ecc workspace flow.
# Usage:
#   ecc -script run_workspace.tcl <workspace-home-or-root>
#   ecc -script run_workspace.tcl workspace_gcd


proc ws_get {key} {
  return [workspace_get -key $key]
}

proc ws_step_get {step key} {
  return [workspace_step_get -step $step -key $key]
}

set ::ecc_workspace_root ""
set ::ecc_pdk_root ""

proc set_workspace {path} {
  if {$path eq ""} {
    error "set_workspace requires a non-empty path"
  }
  set ::ecc_workspace_root [file normalize $path]
  return $::ecc_workspace_root
}

proc set_pdk {path} {
  if {$path eq ""} {
    error "set_pdk requires a non-empty path"
  }
  set ::ecc_pdk_root [file normalize $path]
  return $::ecc_pdk_root
}

proc path_has_prefix {path prefixes} {
  foreach prefix $prefixes {
    if {$path eq $prefix || [string first "$prefix/" $path] == 0} {
      return 1
    }
  }
  return 0
}

proc workspace_path {path} {
  if {$path eq ""} {
    return ""
  }

  set trimmed [string trimleft $path /]
  set workspace_prefixes {
    CTS_ecc Floorplan_ecc fixFanout_ecc place_ecc legalization_ecc
    route_ecc drc_ecc filler_ecc origin home
  }

  if {[path_has_prefix $trimmed $workspace_prefixes]} {
    return [file normalize [file join $::ecc_workspace_root $trimmed]]
  }
  if {[file pathtype $path] eq "absolute"} {
    return [file normalize $path]
  }
  return [file normalize [file join $::ecc_workspace_root $path]]
}

proc pdk_path {path} {
  if {$path eq ""} {
    return ""
  }

  set trimmed [string trimleft $path /]
  if {[path_has_prefix $trimmed {IP prtech resource}]} {
    return [file normalize [file join $::ecc_pdk_root $trimmed]]
  }
  if {[file pathtype $path] eq "absolute"} {
    return [file normalize $path]
  }
  return [file normalize [file join $::ecc_pdk_root $path]]
}

proc pdk_path_list {paths} {
  set result {}
  foreach path $paths {
    lappend result [pdk_path $path]
  }
  return $result
}

proc expand_config_text {text} {
  set replacements {}
  foreach prefix {
    CTS_ecc Floorplan_ecc fixFanout_ecc place_ecc legalization_ecc
    route_ecc drc_ecc filler_ecc origin home
  } {
    lappend replacements "\"/$prefix\"" "\"$::ecc_workspace_root/$prefix\""
    lappend replacements "\"/$prefix/" "\"$::ecc_workspace_root/$prefix/"
    lappend replacements "\"$prefix\"" "\"$::ecc_workspace_root/$prefix\""
    lappend replacements "\"$prefix/" "\"$::ecc_workspace_root/$prefix/"
  }
  foreach prefix {IP prtech resource} {
    lappend replacements "\"/$prefix\"" "\"$::ecc_pdk_root/$prefix\""
    lappend replacements "\"/$prefix/" "\"$::ecc_pdk_root/$prefix/"
    lappend replacements "\"$prefix\"" "\"$::ecc_pdk_root/$prefix\""
    lappend replacements "\"$prefix/" "\"$::ecc_pdk_root/$prefix/"
  }
  return [string map $replacements $text]
}

proc config_path {path} {
  if {$path eq ""} {
    return ""
  }

  set source [workspace_path $path]
  if {![file exists $source]} {
    return $source
  }

  set fp [open $source r]
  set text [read $fp]
  close $fp

  set expanded_text [expand_config_text $text]
  if {$expanded_text ne $text} {
    set fp [open $source w]
    puts -nonewline $fp $expanded_text
    close $fp
  }

  return $source
}

proc ensure_parent_dir {path} {
  if {$path ne ""} {
    file mkdir [file dirname $path]
  }
}

proc safe_eval {script} {
  if {[catch {uplevel 1 $script} err opts]} {
    puts stderr "WARNING: $script failed: $err"
    return 0
  }
  return 1
}

proc ecc_init_step {step} {
  puts "==> load data for $step"

  set flow_config [config_path [ws_step_get $step config.flow]]
  set db_config [config_path [ws_step_get $step config.db]]
  set output_dir [workspace_path [ws_step_get $step output.dir]]
  set tech_lef [pdk_path [ws_get pdk.tech_lef]]
  set lefs [pdk_path_list [ws_get pdk.lefs]]
  set input_def [workspace_path [ws_step_get $step input.def]]
  set input_verilog [workspace_path [ws_step_get $step input.verilog]]
  set input_db [workspace_path [ws_step_get $step input.db]]
  set top [ws_get design.top]

  flow_init -config $flow_config
  db_init -config $db_config -output_dir_path $output_dir

  if {$input_db ne "" && [file exists $input_db]} {
    if {[safe_eval [list load_data -path $input_db]]} {
      return
    }
  }

  tech_lef_init -path $tech_lef
  lef_init -path $lefs

  if {$input_def ne "" && [file exists $input_def]} {
    def_init -path $input_def
  } elseif {$input_verilog ne "" && [file exists $input_verilog]} {
    verilog_init -path $input_verilog -top $top
  } else {
    error "no valid input DEF or Verilog for step $step"
  }
}

proc ecc_save_step {step {feature_step 1}} {
  puts "==> save data for $step"

  set output_def [workspace_path [ws_step_get $step output.def]]
  set output_verilog [workspace_path [ws_step_get $step output.verilog]]
  set output_gds [workspace_path [ws_step_get $step output.gds]]
  set output_json [workspace_path [ws_step_get $step output.json]]
  set output_db [workspace_path [ws_step_get $step output.db]]
  set feature_db [workspace_path [ws_step_get $step feature.db]]
  set feature_step_path [workspace_path [ws_step_get $step feature.step]]
  set report_db_path [workspace_path [ws_step_get $step report.db]]
  set sta_dir [workspace_path [ws_step_get $step data.sta]]

  ensure_parent_dir $output_def
  ensure_parent_dir $output_verilog
  ensure_parent_dir $output_gds
  ensure_parent_dir $output_json
  ensure_parent_dir $feature_db
  ensure_parent_dir $feature_step_path
  ensure_parent_dir $report_db_path
  file mkdir $sta_dir

  def_save -path $output_def
  netlist_save -path $output_verilog -exclude_cell_names {}
  safe_eval [list gds_save -path $output_gds]
  json_save -path $output_json
  # safe_eval [list save_data -path $output_db]
  feature_summary -path $feature_db
  if {$feature_step} {
    feature_tool -path $feature_step_path -step $step
  }
  report_db -path $report_db_path

  if {[safe_eval [list init_sta -output $sta_dir]]} {
    safe_eval {report_timing -json}
  }
}

proc ecc_run_floorplan {step} {
  ecc_init_step $step

  set core_margin [ws_get param.Core.Margin]
  set x_margin [lindex $core_margin 0]
  set y_margin [lindex $core_margin 1]
  if {$x_margin eq ""} { set x_margin 0 }
  if {$y_margin eq ""} { set y_margin 0 }
  set aspect_ratio [ws_get {param.Core.Aspect ratio}]

  init_floorplan \
    -core_util [ws_get param.Core.Utilitization] \
    -x_margin $x_margin \
    -y_margin $y_margin \
    -xy_ratio $aspect_ratio \
    -core_site [ws_get pdk.site_core] \
    -io_site [ws_get pdk.site_io] \
    -corner_site [ws_get pdk.site_corner]

  foreach track {
    {MET1 0 200 0 200}
    {MET2 0 200 0 200}
    {MET3 0 200 0 200}
    {MET4 0 200 0 200}
    {MET5 0 200 0 200}
    {T4M2 0 800 0 800}
    {RDL 0 5000 0 5000}
  } {
    lassign $track layer x_start x_step y_start y_step
    gern_track -layer $layer -x_start $x_start -x_step $x_step -y_start $y_start -y_step $y_step
  }

  foreach io {
    {VDD INOUT 1}
    {VDDIO INOUT 1}
    {VSS INOUT 0}
    {VSSIO INOUT 0}
  } {
    lassign $io net direction is_power
    add_pdn_io -net_name $net -direction $direction -is_power $is_power
  }

  foreach connect {
    {VDD VDD 1}
    {VDD VDD1 1}
    {VDD VNW 1}
    {VDDIO VDDIO 1}
    {VSS VSS 0}
    {VSS VSS1 0}
    {VSS VPW 0}
    {VSSIO VSSIO 0}
  } {
    lassign $connect net pin is_power
    global_net_connect -net_name $net -instance_pin_name $pin -is_power $is_power
  }

  auto_place_pins -layer MET3 -width 300 -height 600

  tapcell -tapcell [ws_get pdk.tap_cell] -distance 58 -endcap [ws_get pdk.end_cap]

  create_grid -layer_name MET1 -net_name_power VDD -net_name_ground VSS -width 0.16 -offset 0
  create_stripe -layer_name MET4 -net_name_power VDD -net_name_ground VSS -width 1 -pitch 16 -offset 0.5
  create_stripe -layer_name MET5 -net_name_power VDD -net_name_ground VSS -width 1 -pitch 16 -offset 0.5
  connect_two_layer -layers {MET1 MET4}
  connect_two_layer -layers {MET4 MET5}

  set clock_name [ws_get param.Clock]
  if {$clock_name ne ""} {
    set_net -net_name $clock_name -type CLOCK
  }

  ecc_save_step $step 0
}

proc ecc_run_fixfanout {step} {
  ecc_init_step $step
  run_no_fixfanout -config [config_path [ws_step_get $step config.fixFanout]]
  ecc_save_step $step
}

proc ecc_run_place {step} {
  ecc_init_step $step
  safe_eval {destroy_pl}
  run_placer -config [config_path [ws_step_get $step config.place]]
  safe_eval [list feature_eval_map -path [workspace_path [ws_step_get $step feature.map]] -bin_cnt_x 256 -bin_cnt_y 256]
  ecc_save_step $step
  safe_eval {destroy_pl}
}

proc ecc_run_cts {step} {
  ecc_init_step $step
  run_cts -config [config_path [ws_step_get $step config.CTS]] -work_dir [workspace_path [ws_step_get $step data.CTS]]
  cts_report -path [workspace_path [ws_step_get $step data.CTS]]
  ecc_save_step $step
}

proc ecc_run_legalization {step} {
  ecc_init_step $step
  safe_eval {destroy_pl}
  run_incremental_flow -config [config_path [ws_step_get $step config.place]]
  ecc_save_step $step
  safe_eval {destroy_pl}
}

proc ecc_run_route {step} {
  ecc_init_step $step

  init_notification
  init_rt \
    -temp_directory_path [workspace_path [ws_step_get $step data.route]] \
    -bottom_routing_layer [ws_get {param.Bottom layer}] \
    -top_routing_layer [ws_get {param.Top layer}] \
    -thread_number 50 \
    -output_inter_result 0 \
    -enable_notification 0 \
    -enable_timing 0

  run_rt
  destroy_rt
  ecc_save_step $step
}

proc ecc_run_drc {step} {
  ecc_init_step $step
  init_drc -temp_directory_path [workspace_path [ws_step_get $step data.drc]] -thread_number 128
  run_drc -config [config_path [ws_step_get $step config.drc]] -path [workspace_path [ws_step_get $step report.step]]
  ecc_save_step $step
  save_drc -path [workspace_path [ws_step_get $step feature.step]]
}

proc ecc_run_filler {step} {
  ecc_init_step $step
  run_filler -config [config_path [ws_step_get $step config.filler]]
  ecc_save_step $step
}

proc ecc_run_step {step} {
  puts ""
  puts "=============================="
  puts "Running $step"
  puts "=============================="

  switch -- $step {
    Floorplan {
      ecc_run_floorplan $step
    }
    fixFanout {
      ecc_run_fixfanout $step
    }
    place {
      ecc_run_place $step
    }
    CTS {
      ecc_run_cts $step
    }
    legalization {
      ecc_run_legalization $step
    }
    route {
      ecc_run_route $step
    }
    drc {
      ecc_run_drc $step
    }
    filler {
      ecc_run_filler $step
    }
    default {
      error "unsupported ecc workspace step: $step"
    }
  }
}

set script_dir [file normalize [file dirname [info script]]]
set default_workspace [file normalize [file join $script_dir workspace_gcd home]]
set default_pdk [file normalize [file join $script_dir .. .. .. .. icsprout55-pdk]]

if {[llength $argv] > 0} {
  set workspace_home [file normalize [lindex $argv 0]]
} elseif {[info exists ::env(WORKSPACE_HOME)]} {
  set workspace_home [file normalize $::env(WORKSPACE_HOME)]
} else {
  set workspace_home $default_workspace
}

puts "Workspace: $workspace_home"
workspace_load -path $workspace_home
set workspace_root [ws_get workspace.dir]
set_workspace $workspace_root

set pdk_root [ws_get pdk.root]
if {$pdk_root eq ""} {
  set pdk_root $default_pdk
}
set_pdk $pdk_root
puts "PDK: $::ecc_pdk_root"

set force_config 0
if {[info exists ::env(WORKSPACE_FORCE_CONFIG)]} {
  set force_config $::env(WORKSPACE_FORCE_CONFIG)
}
workspace_prepare -force $force_config

set step_count [workspace_step_count]
for {set index 0} {$index < $step_count} {incr index} {
  set step [workspace_step_name -index $index]
  set tool [workspace_step_get -step $step -key tool]

  if {$tool ne "ecc"} {
    puts "Skip $step: unsupported tool $tool"
    continue
  }

  workspace_set_state -step $step -tool $tool -state Ongoing
  if {[catch {ecc_run_step $step} err opts]} {
    workspace_set_state -step $step -tool $tool -state Incomplete
    puts stderr "Step $step failed: $err"
    puts stderr [dict get $opts -errorinfo]
    flow_exit
    exit 1
  }
  workspace_set_state -step $step -tool $tool -state Success
}

flow_exit
