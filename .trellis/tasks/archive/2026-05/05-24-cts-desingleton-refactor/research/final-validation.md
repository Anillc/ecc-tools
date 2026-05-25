# Final Validation · CTS Desingleton Refactor

Date: 2026-05-24

## Architecture Acceptance

- Non-API `_INST` / `getInst()` singleton boundaries were removed from iCTS source, API internals, and tests.
- The only allowed singleton entry remains `CTS_API_INST` / `CTSAPI::getInst()` in `src/operation/iCTS/api`.
- `CTSRuntime` owns `Config`, `Design`, `Wrapper`, `STAAdapter`, `FastSTA`, and `schema::SchemaWriter`; `CTSAPI` owns the runtime and a normal `Flow` object.
- Flow/module dependencies are passed explicitly through references, inputs, and narrow configs instead of `SingletonRegistry`, service locator, global context, reset registry, or deep algorithm `CTSRuntime&`.
- HTree contracts were split into `HTreeInput`, `HTreeConfig`, `HTreeOutput`, and `HTreeSummary`; legacy `HTreeSynthesisOptions` and `HTreeSynthesisResult` files were removed.
- Test fixtures use explicit runtime ownership, including same-process runtime isolation coverage.
- Specs were updated to encode the new iCTS internal singleton ban, runtime ownership rule, input/config/output/summary convention, config minimization, and output/summary separation.

## Validation Commands

```bash
targets=$(ninja -C build -t targets | rg '^icts_test_[^:]+: phony' | cut -d: -f1 | tr '\n' ' ')
ninja -C build $targets
```

Result: passed.

```bash
ctest --test-dir build -R '^icts_test_' --output-on-failure
```

Result: passed, 15/15 tests.

```bash
bash build.sh -y
```

Result: passed.

```bash
cp -f /home/liweiguo/project/ecc-tools/bin/iEDA /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev/iEDA
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Result: passed with `iCTS run successfully.`

Generated representative artifacts:

- `scripts/design/ics55_dev/result/iCTS_result.def`
- `scripts/design/ics55_dev/result/iCTS_result.v`
- `scripts/design/ics55_dev/result/report/cts_stat.json`
- `scripts/design/ics55_dev/result/metric/iCTS_metrics.json`
- `scripts/design/ics55_dev/result/cts/visualization/`

Latest rerun after ensuring the iCTS Tcl scripts create report/metric output directories:

```bash
test -f scripts/design/ics55_dev/result/iCTS_result.def
test -f scripts/design/ics55_dev/result/iCTS_result.v
test -f scripts/design/ics55_dev/result/report/cts_stat.json
test -f scripts/design/ics55_dev/result/metric/iCTS_metrics.json
test -d scripts/design/ics55_dev/result/cts/visualization
```

Result: all representative artifacts are present.

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result: passed.

Summary:

- In-scope findings: 0
- Out-of-scope findings: 5877
- Triggered by in-scope translation units: 5877
- Total runtime: 513.981s on the latest run
- Per-kind in-scope findings: format 0, tidy 0, headers 0, cmake 0, iwyu 0

```bash
rg -n '\b[A-Z][A-Z0-9_]*_INST\b|getInst\(' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
```

Result: only allowed API singleton hits remain:

- `src/operation/iCTS/api/CTSAPI.hh`: `CTS_API_INST` and `CTSAPI::getInst()`
- `src/operation/iCTS/api/CTSAPI.cc`: API static wrapper calls through `CTSAPI::getInst()`

```bash
rg -n 'STAAdapter::[A-Za-z_][A-Za-z0-9_]*\s*\(' src/operation/iCTS/source src/operation/iCTS/test src/operation/iCTS/api | rg -v 'auto STAAdapter::|".*STAAdapter::'
```

Result: no static-through-instance call sites.

```bash
rg -n 'CTSContext|SingletonRegistry|ServiceLocator|ResetRegistry|GlobalContext' src/operation/iCTS/source src/operation/iCTS/test .trellis/spec/backend .trellis/spec/guides
```

Result: no matches.

```bash
rg -n 'SharedRuntime|HTreeBuildResult|HTree::BuildResult|HTreeSynthesisOptions|HTreeSynthesisResult|Topology::BuildResult|SourceTrunkBuildOptions|SourceTrunkBuildResult|\bBuildOptions\b' src/operation/iCTS/source src/operation/iCTS/test
```

Result: no matches.

```bash
git diff --check
```

Result: passed.

## Residual Note

Root-level untracked `tmpkwi7mspm.cc` existed at handoff and was left untouched because it is outside the iCTS task scope.
