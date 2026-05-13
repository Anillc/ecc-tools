# align main branch implementation

## Implementation Checklist

- [x] Confirm user approval to start implementation after origin report.
- [x] Run baseline iCTS dev script before restore and save output/log checksum.
- [x] Restore approved category 3 paths from `main`.
- [x] Restore approved category 4 paths from `main`.
- [x] Inspect remaining `main..cts_refactor` diff after exclusions.
- [x] Build the project.
- [x] Run post-restore iCTS dev script.
- [x] Compare baseline vs post-restore output/results and summarize any differences.
- [ ] Archive task and commit validated changes.

## Validation

- Baseline and post-restore:
  - `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- Build:
  - Use the repository's existing build workflow and report the exact command and result.

## Review Gates

- Before implementation: user reviewed category 3/4 origin report and confirmed start.
- Before commit: user approved direct commit after validation.

## Explicit User Constraints

- Do not run `ecc_dev_tools`; this task does not modify CTS code.
- Do not update specs.
- Do not push.

## Execution Notes

- Baseline iCTS command passed before restore and was saved as `validation/baseline_icts.log`.
- Category 3/4 restores were applied from `main`.
- Clean build command passed after one compatibility fix:
  `./build.sh -d -y`
- The build fix was limited to `src/operation/iPL/source/module/wrapper/IDBWrapper.cc`, changing the iSTA TimingEngine include from `"TimingEngine.hh"` to `"api/TimingEngine.hh"` because the iCTS refactor introduces another `TimingEngine.hh` earlier in the include search path.
- Post-restore iCTS command passed with the rebuilt binary copied to `scripts/design/ics55_dev/iEDA`.
- Baseline and post key CTS results matched:
  - `iCTS_result.def` hash unchanged.
  - `sink_count=8751`
  - `htree_inserted_buffer_count=1381`
  - `final_clock_buffer_count=4392`
  - `final_buffer_area=12592.160 um^2`
  - `max_clock_net_wirelength=525.328 um`
  - `total_clock_network_wirelength=59190.091 um`
  - setup/hold timing overview unchanged: setup TNS/WNS `0.000/7.307`, hold TNS/WNS `0.000/0.028`.
- Raw report hashes/logs differ in volatile fields such as timestamps, elapsed time, peak memory, and equal-slack STA report ordering. No CTS metric or DEF behavior drift was found.
- An extra post/post comparison run was started only to probe report nondeterminism, but it exceeded nine minutes in `StaSlewPropagation` with no log growth and was terminated. It is not used as the task acceptance run.
- After the work commit, the requested binary command was run again:
  `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
  It exited 0 and reported `iCTS run successfully`; key CTS metrics remained unchanged (`sink_count=8751`, `htree_inserted_buffer_count=1381`, `final_clock_buffer_count=4392`, `total_clock_network_wirelength=59190.091 um`).
