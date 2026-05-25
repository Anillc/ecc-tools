# Implementation Plan · CTS Flow Contracts Tests And Spec Finalization

## Steps

- [x] Audit remaining flow structs and globals:
  `rg -n '_INST|Options|Result|getInst' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test`
- [x] Refactor Instantiation contract to input/config/output/summary.
- [x] Refactor Optimization contract to input/config/output/summary.
- [x] Refactor Evaluation contract to input/config/output/summary.
- [x] Refactor Report contract to input/config/output/summary.
- [x] Delete remaining non-`CTS_API_INST` macro definitions and singleton `getInst()` APIs.
- [x] Convert singleton reset fixtures into explicit runtime fixtures.
- [x] Add two-runtime/flow same-process regression coverage.
- [x] Run final grep acceptance.
- [x] Run build/tests and representative iCTS Tcl flow.
- [x] Update `.trellis/spec/` concisely with new development rules.
- [x] Run Trellis check.

## Validation

```bash
rg -n '\b[A-Z][A-Z0-9_]*_INST\b' src/operation/iCTS/source src/operation/iCTS/test src/operation/iCTS/api
bash build.sh
ninja -C build icts_test
cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Use the actual available iCTS test target if `icts_test` is not the target name in the local build tree.

## Risk Files

- `src/operation/iCTS/source/flow/instantiation/*`
- `src/operation/iCTS/source/flow/optimization/*`
- `src/operation/iCTS/source/flow/evaluation/*`
- `src/operation/iCTS/source/flow/report/*`
- `src/operation/iCTS/test/**`
- `.trellis/spec/backend/database-guidelines.md`
- `.trellis/spec/backend/quality-guidelines.md`
- `.trellis/spec/backend/directory-structure.md`

## Rollback Point

Complete code/test final cleanup before spec updates. Spec should describe the final state only, not an intermediate state with some singleton families still present.
