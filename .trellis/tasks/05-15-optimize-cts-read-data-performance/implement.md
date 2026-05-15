# implement.md

## Implementation Checklist

- [x] Record baseline behavior for the default huge dev command after temporarily switching it to `design_clock.def`.
- [x] Inspect existing logs and add focused timing if the current stage metric is not enough.
- [x] Identify the dominant substage of CTS `read_data`.
- [x] Implement a narrow optimization only if the hotspot is in CTS-owned code.
- [x] Build the touched iCTS targets.
- [x] Run focused iCTS tests for flow/design conversion where available.
- [x] Re-run the huge dev command enough to compare `read_data` timing.
- [x] Restore `run_iCTS_dev.tcl` to `design.def`.
- [x] Run Trellis/ecc checks appropriate for the changed CTS module.
- [x] Summarize timing evidence, changed files, and remaining risks.

## Validation

- `cmake --build build --target iEDA -j 15`
- Focused CTS tests discovered from existing targets.
- `cd scripts/design/ics55_huge_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` with a timeout large enough to capture `read_data` and avoid indefinite final-STA blocking.
- `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`

## Final Check

- `cmake --build build --target iEDA icts_test_module_topology_fast_clustering -j 15`
- `./bin/icts_test_module_topology_fast_clustering`
- `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
  - Result: pass, exit code 0.
  - In-scope findings: 0 across `format`, `tidy`, `headers`, `cmake`, and `iwyu`.

## Review Gates

- Do not keep the temporary `design_clock.def` script default after the investigation.
- Do not optimize final STA in this task.
- Do not change user design inputs.
