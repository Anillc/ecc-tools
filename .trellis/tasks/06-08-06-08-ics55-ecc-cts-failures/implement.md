# Implementation Plan

1. Load project development specs with `trellis-before-dev`.
2. Inspect `LibertyExpr` structure and existing Liberty expression traversal helpers.
3. Patch `ClockTraceResolve.cc` so Liberty expression port-name checks do not dereference invalid sentinel pointers.
4. Build the latest binary with `ninja -C build iEDA`.
5. Confirm the rebuilt binary contains the current HTree single-load path and no longer crashes in the clock trace path.
6. Rerun all six ics55 ECC cases with unchanged baseline configs and latest binary.
7. Extract per-case status from `cts.log`, stdout, and process exit code.
8. If cases fail, inspect the relevant CTS stage logs and source code paths for concrete root causes.
9. Write a concise result report and recommended fix list.

## Validation Commands

- `ninja -C build iEDA`
- `strings scripts/design/ics55_dev/iEDA | rg 'trivial_single_load'`
- `python3 scripts/design/ics55_ecc_dev/tools/run_ecc_cts.py --all --force --ieda-binary scripts/design/ics55_dev/iEDA --timeout 300`
- Targeted `rg`/`sed` extraction from `cases/*/result/data/cts/cts.log`

## Rollback Points

- Code changes should be limited to iCTS clock tracing unless later evidence requires additional fixes.
- Temporary rerun outputs should stay in ignored `scripts/design/ics55_ecc_dev` paths or `/tmp`.
- Do not overwrite huangzhipeng source benchmark configs.
