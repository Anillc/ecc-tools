# Implementation Plan

## Implementation Checklist

- [x] Load the applicable project specs before editing production code.
- [x] Inspect current native h-tree, characterization, STA adapter, and reporting code paths.
- [x] Implement `CharBuilder` timing observation boundary semantics.
- [x] Convert characterization wire resistance from milliohms to ohms before STA RC installation.
- [x] Extend root-driver direct Liberty query to return output slew.
- [x] Extend root-driver compensation data/report structures with output slew and closure buckets.
- [x] Add `root_input_slew` config and replace production topology-side `max_buf_tran * 0.5` input slew derivation.
- [x] Add strict root boundary closure while preserving the current fanout pruning order and failure diagnostics.
- [x] Add report rows for root cap/slew closure diagnostics.
- [x] Add or update focused tests where the existing test structure supports this without heavy flow setup.
- [x] Build and run targeted tests.
- [x] Run `ecc dev` verification after development is complete.
- [x] Run binary validation for fanout 4 and fanout 32.
- [x] Collect runtime, QoR, evaluation STA error, and fanout legality status for the final report.

## Validation

Build:

```bash
ninja -C build iEDA
```

Targeted regression tests, if the existing build targets are available:

```bash
ninja -C build icts_test_module_routing
./bin/icts_test_module_routing --gtest_filter=RouterClockTreeTest.BuildFluteClockTreeLegalizesOverlappingTerminals:RouterClockTreeTest.BuildFluteClockTreePreservesTerminalMetadataAndRCTreeCap
```

Final project check:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Binary flow:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Run the binary flow twice by changing `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json`:

- `max_fanout = 4`
- `max_fanout = 32`

## Review Gates

- Do not copy full files from `/home/liweiguo/project/ecc-tools`.
- Do not remove existing fanout legality fields or router legalization behavior.
- Do not introduce analytical h-tree or analytical characterization code.
- Keep specs stable except for the already requested global process rule.
