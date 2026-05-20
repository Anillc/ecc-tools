# Implementation Plan: CTS fanout-4 optimization runtime convergence

## Phase 1: Reproduce And Confirm

- [x] Preserve current default fanout 4 config.
- [x] Inspect the current optimization loop and current killed-run log.
- [x] Confirm the solver spends time in exact full-power candidate scans after target skew is already met.
- [x] Confirm whether per-trial cost changed separately from trial-count growth.

## Phase 2: Source Fix

- [x] Add CTS-specific stop policy to optimization options or solver-local logic.
- [x] Stop or tightly bound exact full-power post-target area recovery for small cases.
- [x] Keep cap/slew legality and final target-skew checks unchanged.
- [x] Avoid generic source names banned by the current naming standard.

## Phase 3: Focused Checks

- [x] Build focused targets:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_flow_optimization iEDA
```

- [x] Run focused tests that cover the touched code when available:

```bash
ctest --test-dir build --output-on-failure -R '^icts_test_'
```

## Phase 4: Binary Validation

- [x] Run fanout 4 default validation:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- [x] Run fanout 32 comparison by changing only `max_fanout` in the local CTS config, then restore fanout 4 afterward.
- [x] Record runtime/QoR in `results.md`.

## Phase 5: Final Check

- [x] Run:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

- [x] Confirm zero in-scope findings.
- [x] Return to the interrupted CTS code-normalization review task.

The final full `src/operation/iCTS` check was completed during the convergence task after the fanout runtime fix and source-structure cleanup had
settled.
