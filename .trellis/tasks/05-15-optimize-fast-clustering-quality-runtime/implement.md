# implement.md

## Implementation Checklist

- [x] Create task and record known huge-case quality/runtime symptoms.
- [x] Inspect FastClustering partition, polish, neighbor search, and finalize implementation.
- [x] Extract current huge-case fanout distribution from generated DEF.
- [x] Add focused internal runtime and fanout-distribution diagnostics to FastClustering.
- [x] Re-run the huge case enough to capture per-phase clustering timing.
- [x] Optimize nearest-neighbor selection/runtime hotspot.
- [x] Improve packing/utilization policy for small `max_fanout`.
- [x] Validate cluster legality and assigned-load completeness.
- [x] Compare before/after cluster count, fanout distribution, FastClustering runtime, downstream HTree runtime, and overall CTS runtime.
- [x] Summarize remaining QoR risk.

## Validation

- Focused unit tests for FastClustering behavior where available or added.
- Targeted huge-case run with `design_clock.def`, `clock`, `max_fanout=4`.
- Build touched iCTS targets.
- Do not run ecc dev checks unless explicitly requested later.

## Validation Log

- `cmake --build build --target iEDA -j 15`
- `cd scripts/design/ics55_huge_dev && ... ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` with `INPUT_DEF=./design_clock.def`, `CTS_CONFIG=research/huge_design_clock_sdc_config.json`, result dir `result_fast_clustering_diagnosis`
- `cmake --build build --target icts_test_module_topology_fast_clustering -j 15`
- `./bin/icts_test_module_topology_fast_clustering`
- `cmake --build build --target iEDA icts_test_module_topology_fast_clustering -j 15`
- `./bin/icts_test_module_topology_fast_clustering`
- Huge-case step validation with `design_clock.def`, `clock`, `use_netlist=OFF`:
  - baseline diagnostics: `result_fast_clustering_diagnosis`
  - P1: `result_fast_clustering_p1_f4`
  - P2: `result_fast_clustering_p2_f4`
  - P3: `result_fast_clustering_p3_f4`
  - P4 F4: `result_fast_clustering_p4_f4`
- P4 fanout sweep: `result_fast_clustering_p4_f8`, `result_fast_clustering_p4_f16`, `result_fast_clustering_p4_f32`, `result_fast_clustering_p4_f64`
- See `research/p1_p4_validation.md` for the measured per-step and multi-fanout results.
- `git diff --check`
- Per user instruction, ecc dev checks were not run during this development loop.
- Final acceptance check after user request:
  - `cmake --build build --target iEDA icts_test_module_topology_fast_clustering -j 15`
  - `./bin/icts_test_module_topology_fast_clustering`
  - `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
  - Result: pass, exit code 0, in-scope findings 0 across `format`, `tidy`, `headers`, `cmake`, and `iwyu`.

## Review Gates

- Do not trade correctness for packing: final fanout/diameter/cap legality remains mandatory.
- Do not broaden this task into analytical H-tree or STA evaluation optimization.
- Keep diagnostics useful but not noisy.
