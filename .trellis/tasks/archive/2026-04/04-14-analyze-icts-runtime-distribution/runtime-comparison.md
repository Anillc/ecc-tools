# Runtime Comparison Notes

## Scope

This note records the post-`v3` incremental runtime experiments requested by the human:

- `step1`: remove hot-path `iPA` prepared-internal-power `INFO/Stats`
- `step2`: keep `step1`, then add prepared selected-net switch-power context

All measurements below use existing tests only. Runtime-analysis instrumentation is still temporary and must be cleaned before `$finish-work`.

## Artifact Snapshots

- `v3` baseline snapshots:
  - `runtime_snapshots/v3_baseline_smoke_manual_htree_report.txt`
  - `runtime_snapshots/v3_baseline_htree_report.log`
- `step1` snapshots:
  - `runtime_snapshots/step1_smoke_manual_htree_report.txt`
  - `runtime_snapshots/step1_htree_report.log`
- `step2` snapshots:
  - `runtime_snapshots/step2_smoke_manual_htree_report.txt`
  - `runtime_snapshots/step2_htree_report.log`

## Validation Used

- `./bin/icts_test_module_characterization_realtech --gtest_filter=CharacterizationRealTechSmokeTest.LegacyAndOptimizedExternalRuntimeProduceIdenticalChars`
- `./bin/icts_test_module_characterization_realtech --gtest_filter=CharacterizationRealTechSmokeTest.ManualHTreeCompositionProducesInspectableReport`
- `./bin/icts_test_flow_htree_realtech --gtest_filter=HTreeBuilderRealTechSmokeTest.LegacyAndOptimizedRuntimeProduceIdenticalHTreeBuildResult`
- `./bin/icts_test_flow_htree_realtech --gtest_filter=HTreeBuilderRealTechSmokeTest.SynthesizesMaterializedHTreeFromRealClockLoads`

## Measurement Summary

### Baselines

- `v2` characterization build: `21791.649 ms`
- `v2` HTree total: `21906.391 ms`
- `v3` characterization build: `18104.437 ms`
- `v3` HTree total: `19154.449 ms`

### step1

- characterization build: `17863.029 ms`
- HTree total: `20360.800 ms`
- characterization delta vs `v3`: `-241.408 ms (-1.33%)`
- characterization delta vs `v2`: `-3928.620 ms (-18.03%)`
- HTree delta vs `v3`: `+1206.351 ms (+6.30%)`
- HTree delta vs `v2`: `-1545.591 ms (-7.06%)`

### step2

- characterization build: `18230.565 ms`
- HTree total: `20453.545 ms`
- characterization delta vs `v3`: `+126.128 ms (+0.70%)`
- characterization delta vs `v2`: `-3561.084 ms (-16.34%)`
- HTree delta vs `v3`: `+1299.096 ms (+6.78%)`
- HTree delta vs `v2`: `-1452.846 ms (-6.63%)`

## Important Interpretation Note

Current public runtime buckets do not isolate `refreshCharPowerOptimizedLoad()`. That means the selected-net preparation work added in `step2` is only partially visible in the stage report.

From the current characterization reports:

- `step1` hidden/unbucketed time: `6057.275 ms`
- `step2` hidden/unbucketed time: `5523.253 ms`
- hidden bucket delta: `-534.022 ms (-8.82%)`

This hidden-bucket reduction is consistent with `step2` reducing the uninstrumented selected-net refresh path, even though the single-run end-to-end total was dominated by timing-side runtime variance.
