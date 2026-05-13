# align main branch design

## Technical Design

The cleanup is a selective restore on `cts_refactor`, not a merge or rebase.

Restore to `main`:

- Category 3 non-CTS drift:
  - global build/CMake/Rust CMake drift
  - iMP, py_imp, iIR, iRT/eval/iPL, operation-level CMake
  - iSTA test/debug/report-only files identified as non-required by iCTS
- Category 4 DB sync drift:
  - `IdbInstance.*`
  - non-CTS `TimingIDBAdapter` cell-substitution DB sync changes

Preserve from `cts_refactor`:

- `src/operation/iCTS/**`
- CTS-facing Tcl/Python/tool-manager interfaces
- CTS config files and default config schema
- iCTS-required iSTA/liberty/iPA behavior:
  - char-only timing update APIs
  - SDC clock-only reader
  - liberty unit conversion for transition/leakage/internal-power semantics
  - iPA power unit alignment

## Compatibility

After restore, iCTS should build and produce the same `ics55_dev` run behavior as before cleanup. Remaining non-iCTS diffs should be limited to CTS-required interfaces and semantic dependencies.

## Rollback

Before editing, capture:

- git status
- baseline iCTS dev script output

Rollback is `git restore` of edited paths from `HEAD` before committing, or a new selective restore from `cts_refactor` if a path proves required.
