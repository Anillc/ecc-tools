# Review Round 10 Summary

## Work Completed
- Fixed `[P1]` in `StaClusterTiming::addHierSubNetlist()` by stopping the port-handling branch from falling through into `Instance new_inst(*instance)` when the current cluster member is a top-level port. The same guard was applied in the one-instance-cluster debug path for the same reason.
- Fixed `[P1]` in `Instance::Instance(const Instance&)` by rebinding every cloned pin to the copied instance with `pin->set_own_instance(this)`, so subnetlist-cloned pins no longer retain dangling ownership back to the original top netlist instance.
- Fixed `[P2]` in `Port` copy/move semantics by preserving `_caps` across copy construction, move construction, and move assignment, so boundary ports keep any previously applied load data when they are copied/moved into subnetlists.
- Added focused regressions for all three review findings:
  - a death test for cluster members that are top-level ports on all-port nets;
  - an instance-copy ownership test;
  - a moved-port capacitance preservation test.

## Files Changed
- `src/operation/iSTA/source/module/sta/StaClusterTiming.cc`
- `src/operation/iSTA/source/module/netlist/Instance.cc`
- `src/operation/iSTA/source/module/netlist/Port.cc`
- `src/operation/iSTA/test/RestoredClusterTimingTest.cc`

## Validation
- `cmake --build build --target iSTATest -j8`
- `./bin/iSTATest --gtest_filter='RestoredClusterTimingTest.hier_subnetlist_cluster_ports_do_not_dereference_null_instances:RestoredClusterTimingTest.instance_copy_rebinds_cloned_pin_owners_to_the_new_instance:RestoredClusterTimingTest.moved_ports_preserve_capacitance_data:RestoredClusterTimingTest.subnetlist_reuses_shared_top_ports_and_keeps_boundary_ports_connected'`
  - passed `4/4`
- `./bin/iSTATest --gtest_filter='AssignMergeTest.*:RestoredBehaviorTest.*:RestoredStaBehaviorTest.*:LibertyWriterUnitTest.*:TimingEngineTest.read_design_does_not_require_top_module_up_front:RestoredClusterTimingTest.*:LibertyTest.rust_reader_same_sense_arc_sets_prefer_declared_default_arc:LibertyTest.same_sense_arc_sets_without_fallback_keep_all_matching_arcs'`
  - passed `26/26`
- `./bin/iSTATest --gtest_filter='LibertyAlignmentTest.*'`
  - passed `26/26`

## Remaining Items
- None for the round-10 review findings.

## BitLesson Delta
- Action: none
- Lesson ID(s): NONE
- Notes: Re-read `.humanize/bitlesson.md` and attempted `bitlesson-selector "src/operation/iSTA/source/module/sta/StaClusterTiming.cc:107" "src/operation/iSTA/source/module/netlist/Instance.cc:37-49" "src/operation/iSTA/source/module/netlist/Port.cc:41-47"`, but the command is unavailable in this environment (`/usr/bin/bash: bitlesson-selector: command not found`), so no lesson entry was added or updated.
