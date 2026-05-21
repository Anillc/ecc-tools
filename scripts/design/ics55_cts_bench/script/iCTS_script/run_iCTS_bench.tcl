set FLOW_CONFIG $::env(BENCH_FLOW_CONFIG)
set DB_CONFIG $::env(BENCH_DB_CONFIG)
set CTS_CONFIG $::env(BENCH_CTS_CONFIG)
set RESULT_DIR $::env(BENCH_RESULT_DIR)

set CTS_WORK_DIR [file join $RESULT_DIR cts]
set OUTPUT_DEF [file join $RESULT_DIR cts.def]
set OUTPUT_VERILOG [file join $RESULT_DIR cts.v]

proc read_db_config_value {config_path py_code} {
  return [string trim [exec python3 -c $py_code $config_path]]
}

set TECH_LEF_PATH [read_db_config_value $DB_CONFIG {
import json, sys
with open(sys.argv[1], encoding="utf-8") as fp:
    print(json.load(fp)["INPUT"]["tech_lef_path"])
}]

set LEF_PATH_RAW [read_db_config_value $DB_CONFIG {
import json, sys
with open(sys.argv[1], encoding="utf-8") as fp:
    print("\n".join(json.load(fp)["INPUT"]["lef_paths"]))
}]
if {$LEF_PATH_RAW eq ""} {
  set LEF_PATH {}
} else {
  set LEF_PATH [split $LEF_PATH_RAW "\n"]
}

set INPUT_DEF [read_db_config_value $DB_CONFIG {
import json, sys
with open(sys.argv[1], encoding="utf-8") as fp:
    print(json.load(fp)["INPUT"]["def_path"])
}]

file mkdir $RESULT_DIR
file mkdir $CTS_WORK_DIR

#===========================================================
##   init flow config
#===========================================================
flow_init -config $FLOW_CONFIG

#===========================================================
##   read db config
#===========================================================
db_init -config $DB_CONFIG

#===========================================================
##   read lef / def from db config
#===========================================================
tech_lef_init -path $TECH_LEF_PATH
lef_init -path $LEF_PATH
def_init -path $INPUT_DEF

#===========================================================
##   run CTS
#===========================================================
run_cts -config $CTS_CONFIG -work_dir $CTS_WORK_DIR

def_save -path $OUTPUT_DEF

#===========================================================
##   save netlist
#===========================================================
netlist_save -path $OUTPUT_VERILOG -exclude_cell_names {}

#===========================================================
##   report CTS
#===========================================================
cts_report -path $CTS_WORK_DIR

#===========================================================
##   final design power through iPA
#===========================================================
if {![info exists ::env(BENCH_SKIP_POWER)] || $::env(BENCH_SKIP_POWER) ne "1"} {
  set POWER_WORK_DIR [file join $CTS_WORK_DIR power]
  file mkdir $POWER_WORK_DIR

  if {[llength [info commands set_pwr_design_workspace]] > 0 && [llength [info commands report_power]] > 0} {
    if {[catch {
      set_pwr_design_workspace $POWER_WORK_DIR
      report_power -json -toggle 0.1
    } power_error power_options]} {
      puts "WARN: iPA final design power report failed: $power_error"
      set fp [open [file join $POWER_WORK_DIR power_failed.txt] "w"]
      puts $fp $power_error
      close $fp
    }
  } else {
    puts "WARN: iPA report_power command is unavailable in this iEDA binary."
    set fp [open [file join $POWER_WORK_DIR power_unavailable.txt] "w"]
    puts $fp "report_power command unavailable"
    close $fp
  }
}

#===========================================================
##   Exit
#===========================================================
flow_exit
