# iCTS ecc_dev_tools Runtime Analysis

Date: 2026-05-16

## Scope

This analysis covers the current repository-local iCTS checker:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

The code path is under `.trellis/ecc_dev_tools/`.

## Relevant Code Structure

- `.trellis/ecc_dev_tools/check.py`
  - CLI arguments include `--preset`, `--tidy-mode`, `--pass-plan`, `--kinds`, `--runtime-logging`, and `--runtime-detail`.
  - The default command resolves the profile, plan, scope, build context, and runs selected checks.
- `.trellis/ecc_dev_tools/profiles.py`
  - iCTS profile default preset: `default`.
  - Default preset kinds: `format`, `tidy`, `headers`, `cmake`, `iwyu`.
  - Default tidy mode: `deep`.
  - Default pass plan: `complete`.
  - Deep tidy pass sequence: `tidy-tu`, `analyzer-tu`, `tidy-headers`, `clang-frontend`, plus on-demand `native-fallback`.
- `.trellis/ecc_dev_tools/checkers.py`
  - `run_selected_checks()` executes check kinds serially and records `runtime_seconds`.
  - `_parallel_map()` is the shared thread-pool helper.
  - TU-oriented tidy, clang frontend, native fallback, header self-check, dependency scan, format, and IWYU use `_parallel_map()`.
  - `_run_clang_tidy_header_pass()` currently loops over headers serially.
  - `run_iwyu_check()` runs IWYU over all scoped compile commands and records top unit runtimes.

## Environment Baseline

Command:

```bash
python3 ./.trellis/ecc_dev_tools/check.py doctor --profile icts --build-dir build
```

Observed:

- Python: 3.13.12
- CPU threads: 32
- idle estimate: 32
- default jobs: 16
- Required tools present:
  - `cmake` 4.3.0
  - `ninja` 1.10.0
  - `clang-format` 22.1.2
  - `clang-tidy` 22.1.2
  - `clang++` 22.1.2
  - `g++` 10.5.0
- Optional tools present:
  - `clang-scan-deps` 22.1.2
  - `include-what-you-use` 0.26
- Existing build context reused:
  - `build/compile_commands.json`
  - `build/.cmake/api/v1/reply`
  - `build/.ecc_dev_tools/cmake_trace.json`

## Scope Counts

- iCTS profiled source/header files: 337
  - `.cc`: 186
  - `.hh`: 151
- iCTS compile commands in `build/compile_commands.json`: 191
- Header/dependency analysis resolved iCTS targets: 101
- CMake link visibility analyzed targets: 85

## Full Baseline Command

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

Result:

- Exit code: 0
- Wall time: 569.365s
- Runtime reported by checker results: 567.716s
- Final JSON summary after suppressions:
  - in-scope findings: 0
  - out-of-scope findings: 5119
  - total findings: 5119

Note: runtime progress logs are emitted before suppression filtering. The IWYU phase reported 153 in-scope findings before suppression; the final JSON summary reported zero in-scope findings after suppression filtering.

## Runtime Distribution By Check Kind

| Check kind | Runtime | Share | Count / notes |
| --- | ---: | ---: | --- |
| `tidy` | 319.318s | 56.2% | 191 compile commands, 151 headers, 5 planned tidy passes |
| `iwyu` | 210.705s | 37.1% | 191 translation units |
| `headers` | 34.805s | 6.1% | 151 headers, 191 scoped compile commands |
| `cmake` | 2.658s | 0.5% | 101 iCTS targets |
| `format` | 0.229s | ~0.0% | 337 C/C++ files |

Interpretation:

- `tidy` and `iwyu` account for about 93.3% of full runtime.
- `format` and `cmake` are not meaningful optimization targets for wall-clock runtime.
- `headers` is a secondary target, mainly for header self-check parallelism/command cost, but much smaller than `tidy` and `iwyu`.

## Tidy Phase Distribution

| Tidy phase | Runtime | Count | Notes |
| --- | ---: | ---: | --- |
| `tidy-headers` | 125.820s | 151 | Largest tidy subphase; currently serial over headers |
| `analyzer-tu` | 91.785s | 191 | Parallel clang-tidy analyzer pass |
| `tidy-tu` | 56.669s | 191 | Parallel deep tidy TU pass |
| `clang-frontend` | 32.071s | 191 | Parallel clang++ syntax-only diagnostics |
| `native-fallback` | 12.797s | 165 | Parallel on-demand g++ fallback |

Key finding:

- `tidy-headers` is the largest single subphase and is structurally different from the rest of the heavy tidy phases because it runs headers in a plain `for` loop instead of `_parallel_map()`.
- Header tidy also builds large per-header `clang-tidy` commands using many `--extra-arg=-I...` entries. During process sampling, header tidy command lines included a very large include-dir set.

## Top Slow Tidy Units

Top entries reported by `--runtime-detail`:

- 35.771s `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc`
- 35.768s `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc`
- 35.505s `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc`
- 32.701s `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc`
- 31.742s `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc`
- 31.642s `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc`
- 31.448s `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc`
- 30.932s `src/operation/iCTS/source/database/adapter/sta/STAAdapterCellQuery.cc`
- 30.179s `src/operation/iCTS/source/database/adapter/sta/STAAdapterClockLookup.cc`
- 30.146s `src/operation/iCTS/source/database/adapter/sta/STAAdapterRootDriverQuery.cc`

Interpretation:

- STA adapter TUs and RealTech/test synthesis TUs are expensive under Clang tooling.
- Duplicate labels can come from different tidy phases over the same TU because runtime details are top entries across tidy sub-runs.

## Header Check Distribution

| Header phase | Runtime | Count | Notes |
| --- | ---: | ---: | --- |
| `header-self-check` | 32.606s | 151 | Dominates `headers`; runs each header self-check and include-first check |
| `dependency-scan` | 2.060s | 191 | Uses `clang-scan-deps`; not a major bottleneck in this run |

Top slow header units:

- 19.926s `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.hh`
- 14.961s `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh`
- 12.948s `src/operation/iCTS/source/database/adapter/sdc/SdcClockReader.hh`
- 12.773s `src/operation/iCTS/source/database/adapter/sdc/SdcClockModel.hh`
- 12.768s `src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.hh`
- 11.042s `src/operation/iCTS/test/flow/synthesis/htree/HTreeVisualizationInternal.hh`
- 10.686s `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.hh`
- 10.685s `src/operation/iCTS/test/flow/synthesis/TopologyVisualizationSupport.hh`
- 10.547s `src/operation/iCTS/test/flow/synthesis/TopologyRealTechSmokeSupport.hh`
- 10.397s `src/operation/iCTS/test/flow/synthesis/htree/HTreeVisualizationSupport.hh`

Interpretation:

- Header self-check is parallelized, but expensive headers are still mostly STA adapter, SDC adapter, and RealTech/test visualization support headers.
- The header dependency scan was not the structural bottleneck in this run despite process sampling showing `clang-scan-deps` activity.

## IWYU Distribution

`iwyu` runtime:

- 210.705s total
- 191 translation units
- Parallelized with jobs=16

Top slow IWYU units:

- 90.745s `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc`
- 89.190s `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc`
- 88.795s `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc`
- 87.186s `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc`
- 84.655s `src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.cc`
- 81.046s `src/operation/iCTS/source/database/adapter/sta/STAAdapterRootDriverQuery.cc`
- 80.327s `src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc`
- 80.111s `src/operation/iCTS/source/database/adapter/sta/STAAdapterCellQuery.cc`
- 79.643s `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharTiming.cc`
- 79.558s `src/operation/iCTS/source/database/adapter/sta/STAAdapterClockLookup.cc`

Interpretation:

- IWYU is already parallelized, so most speedup must come from reducing the TU set, reducing command work, changing when it runs, caching, or splitting default vs exhaustive validation.
- STA adapter TUs are the dominant IWYU cost.

## Initial Optimization Opportunities

1. Parallelize `tidy-headers`.
   - Expected effect: direct reduction of the 125.820s largest tidy subphase.
   - Risk: more simultaneous `clang-tidy` header processes could increase memory/CPU pressure; should reuse `jobs` and preserve deterministic result aggregation.

2. Cache or reuse expensive per-target include-dir resolution.
   - Header tidy and header self-check repeatedly build include-dir sets from target ownership and dependencies.
   - This likely helps Python orchestration and command construction more than external tool runtime, so it is secondary.

3. Improve runtime reporting for phase-specific top units.
   - Current top tidy unit details combine multiple tidy phases. Adding phase labels to unit entries would make future optimization attribution clearer.
   - This is observability, not a direct speedup.

4. Analyze and reduce genuinely overweight translation units where that preserves coverage and behavior.
   - The current evidence shows line count alone is not the main cause of the worst IWYU cost, but large files can still increase tidy/analyzer work and should be triaged separately from dependency weight.

## Coverage Policy Decision

The user explicitly rejected runtime optimization by "shortcut" coverage reduction.

Therefore the optimization scope must preserve current full-check coverage:

- Do not remove `iwyu` from the default validation path as the runtime fix.
- Do not skip `tidy-headers` as the runtime fix.
- Do not add broad suppressions, file skip lists, or special cases for slow files as the runtime fix.
- Do not introduce a fast preset that checks less than the current default and present that as the solution.
- Prefer structural improvements: parallelism reuse, deterministic scheduling, dependency hygiene, translation-unit decomposition where code or include structure is actually overweight, and observability improvements.

## Code Size vs Tool Runtime

The current slow files are not explained by source line count alone.

### Evidence From Full Baseline

Full baseline top `iwyu` files were dominated by STA and SDC adapter TUs:

| File | Lines | Bytes | Direct includes | Compile include flags | IWYU runtime |
| --- | ---: | ---: | ---: | ---: | ---: |
| `source/database/adapter/sta/STAAdapterInternal.cc` | 704 | 27594 | 46 | 225 | 90.745s |
| `source/database/adapter/sta/STAAdapterTimingUpdate.cc` | 507 | 17779 | 32 | 225 | 89.190s |
| `source/database/adapter/sta/STAAdapterCharPower.cc` | 258 | 8359 | 29 | 225 | 88.795s |
| `source/database/adapter/sta/STAAdapter.cc` | 85 | 2893 | 10 | 225 | 87.186s |
| `source/database/adapter/sdc/ClockTraceResolver.cc` | 1121 | 38826 | 30 | 225 | 84.655s |
| `source/database/adapter/sta/STAAdapterRootDriverQuery.cc` | 249 | 9692 | 20 | 225 | 81.046s |
| `source/database/adapter/sta/STAAdapterRcTree.cc` | 222 | 7113 | 22 | 225 | 80.327s |
| `source/database/adapter/sta/STAAdapterCellQuery.cc` | 334 | 13215 | 23 | 225 | 80.111s |
| `source/database/adapter/sta/STAAdapterCharTiming.cc` | 133 | 6340 | 17 | 225 | 79.643s |
| `source/database/adapter/sta/STAAdapterClockLookup.cc` | 128 | 4384 | 17 | 225 | 79.558s |

Largest iCTS `.cc` files by line count at the same time included several files that were not among the slowest IWYU units:

| Lines | File | IWYU/tidy hotspot status |
| ---: | --- | --- |
| 1919 | `test/module/characterization/CharacterizationRealTechExactRegressionTest.cc` | Not in full IWYU top 10 |
| 1591 | `test/flow/FlowTest.cc` | Not in full IWYU top 10 |
| 1525 | `source/flow/synthesis/htree/analytical_solver/AnalyticalSolver.cc` | Not in full IWYU top 10 |
| 1337 | `source/flow/synthesis/htree/HTree.cc` | Not in full IWYU top 10 |
| 1273 | `source/database/adapter/sdc/SdcClockReader.cc` | Not in full IWYU top 10 |
| 1121 | `source/database/adapter/sdc/ClockTraceResolver.cc` | IWYU hotspot |
| 1121 | `source/database/io/Wrapper.cc` | Not in full IWYU top 10 |
| 990 | `source/flow/evaluation/qor/QorEvaluation.cc` | Not in full IWYU top 10 |
| 831 | `source/flow/synthesis/htree/compensation/RootDriverCompensation.cc` | Not in full IWYU top 10 |
| 806 | `source/utils/logger/Schema.cc` | Not in full IWYU top 10 |

This means file size is a useful triage signal, but it is not a reliable predictor of the worst tool runtime by itself.

### Representative Sample Run

Command shape:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS/test/module/characterization/CharacterizationRealTechExactRegressionTest.cc \
  --path src/operation/iCTS/source/flow/synthesis/htree/HTree.cc \
  --path src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc \
  --path src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc \
  --path src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.cc \
  --path src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc \
  --profile icts \
  --build-dir build \
  --kinds tidy,iwyu \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail
```

Result:

- Exit code: 0
- Wall time: 150.605s
- Reported runtime total: 148.971s
- `tidy`: 73.401s
- `iwyu`: 75.570s
- Final in-scope findings after suppressions: 0

Sample IWYU ranking:

| File | Lines | IWYU runtime |
| --- | ---: | ---: |
| `source/database/adapter/sta/STAAdapterInternal.cc` | 704 | 75.399s |
| `source/database/adapter/sta/STAAdapter.cc` | 85 | 72.226s |
| `source/database/adapter/sdc/ClockTraceResolver.cc` | 1121 | 71.118s |
| `source/flow/synthesis/htree/HTree.cc` | 1337 | 16.452s |
| `test/module/characterization/CharacterizationRealTechExactRegressionTest.cc` | 1919 | 13.766s |
| `test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc` | 290 | 11.715s and 11.321s for two compile commands |

Sample tidy hot entries:

| File | Lines | Tidy runtime context |
| --- | ---: | --- |
| `test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc` | 290 | 31.743s and 31.516s in top entries |
| `source/database/adapter/sta/STAAdapterInternal.cc` | 704 | 27.938s, 11.631s, 11.303s in top entries |
| `source/database/adapter/sta/STAAdapter.cc` | 85 | 27.683s, 11.319s, 11.229s in top entries |
| `source/database/adapter/sdc/ClockTraceResolver.cc` | 1121 | 25.855s, 10.553s, 10.418s in top entries |
| `source/flow/synthesis/htree/HTree.cc` | 1337 | 21.127s, 6.245s, 2.591s, 1.942s in top entries |
| `test/module/characterization/CharacterizationRealTechExactRegressionTest.cc` | 1919 | 18.671s, 3.135s, 1.990s, 1.685s in top entries |

Interpretation:

- The 85-line `STAAdapter.cc` is much slower under IWYU than the 1919-line characterization regression test and the 1337-line H-tree implementation file.
- The strongest predictor in the worst IWYU set is not line count, but heavy adapter dependency shape: about 225 compile include flags, command length around 15.7K characters, and direct inclusion of iSTA/iDB/Liberty/Power-facing headers through adapter implementation files.
- `ClockTraceResolver.cc` is both large and dependency-heavy, so it is a genuine candidate for future behavior-preserving decomposition or dependency cleanup.
- Large files such as `CharacterizationRealTechExactRegressionTest.cc`, `FlowTest.cc`, `AnalyticalSolver.cc`, and `HTree.cc` should be triaged for maintainability and tidy/analyzer cost, but current data does not make them the primary IWYU bottleneck.

### Dependency Shape Evidence

Slow STA adapter implementation files pull in heavy external-facing headers such as:

- `api/TimingEngine.hh`
- `api/TimingIDBAdapter.hh`
- `api/Power.hh`
- `sta/Sta.hh`
- `sta/StaData.hh`
- `sta/StaVertex.hh`
- `liberty/Lib.hh`
- `TimingDBAdapter.hh`
- `IdbDesign.h`
- `IdbInstance.h`
- `PwrGraph.hh`
- `PwrVertex.hh`

`STAAdapter.hh` itself is comparatively light and uses forward declarations for iSTA/iPower types. The bottleneck is mostly in implementation TUs that need the external adapter internals, not in public-header line count.

Archived task context supports this distinction:

- `.trellis/tasks/archive/2026-04/04-24-cts-dev-check-scalability/` previously decomposed truly overweight TUs and brought the full iCTS check from manual non-convergence to completion.
- That task's final largest remaining file after split was below about 800 lines at the time.
- Current code has grown new or changed large files again, but the current worst IWYU set now points more strongly at adapter dependency fan-in than at a single monolithic source file.

### Current Recommendation On Code Volume

Use two separate triage tracks:

1. **Tool-runtime blockers by dependency weight**:
   - `STAAdapter*.cc`
   - `ClockTraceResolver.cc`
   - possibly other SDC/Wrapper adapter TUs with 225 include flags and broad external dependencies
   - Goal: reduce heavy include exposure where behavior-preserving, improve target/include hygiene, and keep adapter boundaries intact.

2. **Large-file maintainability and tidy/analyzer cost**:
   - `CharacterizationRealTechExactRegressionTest.cc`
   - `FlowTest.cc`
   - `AnalyticalSolver.cc`
   - `HTree.cc`
   - `SdcClockReader.cc`
   - `ClockTraceResolver.cc`
   - `Wrapper.cc`
   - `QorEvaluation.cc`
   - Goal: split only where there is a natural CTS/EDA responsibility boundary, not as a blanket line-count exercise.

For this task, the implementation priority remains `tidy-headers` parallelism first. Code-volume or dependency-shape refactors should be planned as follow-up implementation slices after the checker-level parallelism change, because they touch product/test code and carry higher behavioral risk.

## Recommended Next Step

Before implementation, create `design.md` and `implement.md` around the non-shortcut plan:

- First slice: parallelize `tidy-headers` using the existing `_parallel_map()`/`jobs` model and add unit coverage for deterministic aggregation.
- Second slice: improve runtime reporting so tidy unit details carry phase context.
- Third slice: evaluate adapter dependency-weight cleanup and large-file decomposition candidates without reducing checker coverage.

## Post-Change Result: tidy-headers Parallelism

Date: 2026-05-16

Implementation:

- `_run_clang_tidy_header_pass()` now accepts `jobs`.
- `_run_tidy_pass()` passes the existing checker `jobs` value into the header tidy pass.
- Header tidy command construction and parsing run through `_parallel_map()`.
- Successful header results are aggregated by repo-relative header path, not completion order, preserving deterministic findings/runtime aggregation for this pass.
- Default presets, tidy checks, IWYU checks, suppressions, scope, and coverage were not changed.

Focused unit validation:

```bash
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
```

Result:

- Exit code: 0
- 176 tests passed
- Added coverage verifies `jobs` is passed to `_parallel_map()` and out-of-order parallel header results are aggregated deterministically.

Focused functional validation:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh \
  --path src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.hh \
  --profile icts \
  --build-dir build \
  --kinds tidy \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail
```

Result:

- Exit code: 0
- Final in-scope findings: 0
- Final out-of-scope findings: 0
- Reported runtime total: 1.009s
- `tidy-headers`: 0.928s for 2 headers
- The two header unit runtimes were 0.786s and 0.473s, whose sum is greater than the phase runtime, confirming the real checker path is executing header tidy in parallel.

Full baseline-comparable validation:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Exit code: 0
- Wall time from `/usr/bin/time -p`: 474.42s
- Runtime reported by checker results: 472.768s
- Final JSON summary after suppressions:
  - in-scope findings: 0
  - out-of-scope findings: 5119
  - total findings: 5119

### Runtime Comparison

| Metric | Baseline | Post-change | Delta | Change |
| --- | ---: | ---: | ---: | ---: |
| Wall time | 569.365s | 474.420s | -94.945s | -16.7% |
| Reported checker runtime | 567.716s | 472.768s | -94.948s | -16.7% |
| `tidy` | 319.318s | 217.731s | -101.587s | -31.8% |
| `tidy-headers` | 125.820s | 10.742s | -115.078s | -91.5% |
| `iwyu` | 210.705s | 216.801s | +6.096s | +2.9% |
| `headers` | 34.805s | 35.375s | +0.570s | +1.6% |
| `header-self-check` | 32.606s | 33.149s | +0.543s | +1.7% |

Interpretation:

- The first slice produced a measurable full-check runtime reduction without reducing coverage.
- The intended hotspot moved from `tidy-headers` to the already-parallel TU-oriented passes and IWYU tail tasks.
- `tidy-headers` is no longer a structural bottleneck: it dropped from 125.820s to 10.742s.
- Residual wall time is now dominated by `tidy` TU phases and `iwyu`, especially STA/SDC adapter units.

### Post-Change Runtime Distribution By Check Kind

| Check kind | Runtime | Share of reported runtime |
| --- | ---: | ---: |
| `tidy` | 217.731s | 46.1% |
| `iwyu` | 216.801s | 45.9% |
| `headers` | 35.375s | 7.5% |
| `cmake` | 2.654s | 0.6% |
| `format` | 0.208s | ~0.0% |

### Post-Change Tidy Phase Distribution

| Tidy phase | Runtime | Count | Notes |
| --- | ---: | ---: | --- |
| `analyzer-tu` | 97.168s | 191 | Now the largest tidy subphase |
| `tidy-tu` | 63.133s | 191 | Second largest tidy subphase |
| `clang-frontend` | 33.257s | 191 | Parallel frontend diagnostics |
| `native-fallback` | 13.251s | 165 | Parallel g++ fallback |
| `tidy-headers` | 10.742s | 151 | No longer the largest subphase |

### Remaining Hotspots

Top post-change IWYU units:

| Runtime | File |
| ---: | --- |
| 87.181s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` |
| 86.241s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc` |
| 84.697s | `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` |
| 84.199s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc` |
| 81.612s | `src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.cc` |
| 79.423s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterRootDriverQuery.cc` |
| 78.806s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCellQuery.cc` |
| 78.806s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc` |
| 78.086s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterClockLookup.cc` |
| 77.640s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterWireRc.cc` |

Top post-change tidy unit entries:

| Runtime | File |
| ---: | --- |
| 37.161s | `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` |
| 35.419s | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc` |
| 35.303s | `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc` |
| 34.708s | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc` |
| 34.356s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc` |
| 33.820s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` |
| 33.547s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc` |
| 31.830s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterRootDriverQuery.cc` |
| 31.680s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc` |
| 31.230s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterClockLookup.cc` |

Recommended next optimization direction:

1. Add phase-aware tidy runtime unit attribution so repeated file labels can be mapped to `tidy-tu`, `analyzer-tu`, `clang-frontend`, or `native-fallback`.
2. Investigate no-coverage-loss scheduling improvements for already-parallel TU work, especially IWYU heavy-tail behavior.
3. Plan a separate dependency-hygiene slice for STA/SDC adapter hotspots if scheduling/observability shows the bottleneck is dependency shape rather than runner overhead.

## Post-Change Result: tidy Runtime Unit Phase Labels

Date: 2026-05-16

Implementation:

- Tidy unit runtime labels now include pass context:
  - `tidy-tu:<path>`
  - `analyzer-tu:<path>`
  - `tidy-headers:<path>`
  - `clang-frontend:<path>`
  - `native-fallback:<path>`
- Runtime phase entries remain unchanged.
- JSON schema, CLI options, check commands, check kinds, suppressions, and finding semantics were not changed.

Purpose:

- This is observability, not a direct runtime reduction.
- The previous runtime detail output mixed multiple tidy phases under identical file labels, making it hard to identify whether a slow file came from `tidy-tu`, `analyzer-tu`, `clang-frontend`, or `native-fallback`.
- The new labels preserve the existing `RuntimeEntry` model while making post-run triage phase-aware.

Validation:

```bash
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
```

Result:

- Exit code: 0
- 177 tests passed
- Added coverage verifies `tidy-headers:<path>` appears in runtime detail labels.

Focused functional validation:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh \
  --path src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.hh \
  --profile icts \
  --build-dir build \
  --kinds tidy \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail
```

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Exit code: 0
- Final in-scope findings: 0
- Final out-of-scope findings: 0
- Reported runtime total: 1.021s
- Runtime detail labels included:
  - `tidy-headers:src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh`
  - `tidy-headers:src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.hh`

Current next-step assessment:

- A full validation after the phase-label-only change is not required to prove a runtime improvement because it does not alter scheduling or check execution.
- The next full validation should be run before finish-work to confirm the combined code state remains clean.
- Further runtime reduction now requires either deeper scheduling changes for already-parallel TU work or product-code dependency hygiene in STA/SDC adapter hotspots.

## Final Combined Validation

Date: 2026-05-16

After adding worker-job notes, the unit test suite was run again:

```bash
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
```

Result:

- Exit code: 0
- 178 tests passed

Full final validation with automatic/default jobs:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Exit code: 0
- Wall time from `/usr/bin/time -p`: 627.69s
- Reported checker runtime: 625.858s
- Final in-scope findings after suppressions: 0
- Final out-of-scope findings after suppressions: 5119

Important context:

- Immediately after this run, `doctor` reported:
  - CPU threads: 32
  - idle estimate: 1
  - default jobs: 1
- This explains why default-runtime comparisons can fluctuate heavily: the checker intentionally computes jobs from system load.
- The run still proves final coverage and quality behavior, but it is not the best apples-to-apples runtime comparison against the original jobs=16 baseline.

Full final validation with explicit jobs=16:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --jobs 16 \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Exit code: 0
- Wall time from `/usr/bin/time -p`: 610.82s
- Reported checker runtime: 602.711s
- Final in-scope findings after suppressions: 0
- Final out-of-scope findings after suppressions: 5119
- Notes now record `Worker jobs: 16` for each check kind.

### Final jobs=16 Runtime Comparison

| Metric | Baseline jobs=16 | Final jobs=16 | Delta | Change |
| --- | ---: | ---: | ---: | ---: |
| Wall time | 569.365s | 610.820s | +41.455s | +7.3% |
| Reported checker runtime | 567.716s | 602.711s | +34.995s | +6.2% |
| `tidy` | 319.318s | 353.945s | +34.627s | +10.8% |
| `tidy-headers` | 125.820s | 10.393s | -115.427s | -91.7% |
| `iwyu` | 210.705s | 209.875s | -0.830s | -0.4% |
| `headers` | 34.805s | 34.881s | +0.076s | +0.2% |

Interpretation:

- The structural optimization to `tidy-headers` is stable and large: about 91.7% faster in the final jobs=16 run.
- The final total wall time did not improve in this particular fixed-jobs run because `tidy-tu` was much slower under current machine conditions:
  - baseline `tidy-tu`: 56.669s
  - final jobs=16 `tidy-tu`: 206.931s
- `iwyu` and `headers` were effectively unchanged, which supports the conclusion that the total regression was environmental/load contention or clang-tidy phase variability rather than loss of the header-tidy optimization.
- The earlier post-change full run measured a 94.945s wall-time improvement and a 115.078s `tidy-headers` phase reduction. Taken together, the reliable conclusion is:
  - coverage is preserved
  - header tidy serialization is fixed
  - total runtime is now dominated by TU-level clang-tidy variability and IWYU heavy adapter TUs
  - future runtime work must target repeated/heavy TU analysis or dependency graph weight

### Final Phase-Aware Hotspots

The phase-aware runtime labels now show the slow tidy categories separately.

Top final `tidy-tu` entries:

| Runtime | File |
| ---: | --- |
| 183.129s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc` |
| 178.809s | `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` |
| 174.606s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc` |
| 173.709s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` |
| 170.694s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterRootDriverQuery.cc` |
| 166.447s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc` |
| 165.291s | `src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.cc` |
| 164.161s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterClockLookup.cc` |

Top final `analyzer-tu` entries:

| Runtime | File |
| ---: | --- |
| 36.507s | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc` |
| 35.360s | `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc` |
| 35.217s | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc` |
| 26.398s | `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechFrontierSupport.cc` |
| 25.218s | `src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc` |

Top final IWYU entries:

| Runtime | File |
| ---: | --- |
| 90.480s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` |
| 87.914s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc` |
| 86.773s | `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` |
| 85.940s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc` |
| 84.014s | `src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.cc` |

### Remaining Optimization Assessment

Low-risk runner work completed before the second slice:

- `tidy-headers` now uses existing parallel worker machinery.
- Runtime detail output is phase-aware for tidy unit entries.
- Worker jobs are recorded in notes, making future runtime comparisons interpretable.

Second-slice candidate identified here and implemented below:

- The final fixed-jobs run showed `tidy-tu` could dominate runtime under load, and the existing deep-mode plan ran normal tidy checks and `clang-analyzer-*` as separate clang-tidy invocations over the same compile commands.
- A combined clang-tidy TU pass is a structural optimization, not a coverage shortcut, if it preserves check globs, diagnostic origin mapping, dedupe priorities, fallback behavior, and suppression behavior.

Still not recommended as immediate no-risk changes:

- Increasing default jobs. The final fixed jobs=16 run shows high contention can make TU-level clang tooling much slower.
- Skipping IWYU, slow files, analyzer checks, header checks, or changed-file-only filtering. These violate the user's coverage/quality constraint.

Recommended next work after this task:

1. Create a separate dependency-hygiene task for STA/SDC adapter translation units, because both IWYU and combined tidy hotspots point to the same dependency-heavy adapter graph.
2. Consider an adaptive jobs policy for heavy clang tooling only after measuring memory/load behavior; do not assume more jobs is faster.
3. Consider opt-in clang-tidy check profiling to identify expensive check families without changing default coverage.

## Combined clang-tidy TU Pass Experiment

Date: 2026-05-16

Question:

- Can the current `tidy-tu` and `analyzer-tu` passes be combined into one `clang-tidy` invocation per TU without reducing check coverage?

Why this matters:

- The final phase-aware runtime output shows the remaining `tidy` runtime is dominated by TU-level passes, not header tidy.
- Current deep mode runs `tidy-tu` and `analyzer-tu` as separate `clang-tidy` invocations over the same 191 compile commands, which repeats Clang parsing work.
- `clang-tidy` supports enabling normal tidy checks and `clang-analyzer-*` checks in the same `--checks` string.

Experiment setup:

- Representative files:
  - `src/operation/iCTS/source/database/config/Config.cc`
  - `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc`
  - `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc`
- Runs per file:
  - `tidy`: current deep tidy checks only
  - `analyzer`: `-*,clang-analyzer-*`
  - `combined`: current deep tidy checks plus `clang-analyzer-*`
- Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Results:

| File | `tidy` | `analyzer` | `combined` | Diagnostic comparison |
| --- | ---: | ---: | ---: | --- |
| `Config.cc` | 2.383s | 7.339s | 7.878s | all zero diagnostics |
| `STAAdapter.cc` | 27.262s | 11.149s | 27.270s | all reported the same `clang-diagnostic-return-type` error from external `IdbLayer.h` |
| `HTreeRealTechSmokeSupport.cc` | 4.548s | 30.863s | 31.951s | all zero diagnostics |

Interpretation:

- The combined invocation is faster than running `tidy` plus `analyzer` separately in these samples.
- The combined invocation tends to cost roughly the slower of the two separate passes, not their sum.
- This suggests there is a real future runtime opportunity in reducing duplicate TU parsing.

Why it was not implemented in this slice:

- A default combined pass would change the internal pass structure.
- Finding `origin` and dedupe priority currently distinguish `tidy-tu` from `analyzer-tu`; a combined invocation would need explicit origin mapping by diagnostic category.
- Native fallback candidate logic currently observes each pass separately; a combined pass must prove equivalent behavior when diagnostics are absent, suppressed, or emitted from headers.
- The experiment covered only three files, not the full iCTS finding/suppression space.

Recommended next step:

- Plan a separate implementation slice for a combined clang-tidy TU pass.
- Required proof before making it default:
  - Compare current separate `tidy-tu` + `analyzer-tu` output with combined output on a representative full or large sample.
  - Preserve or explicitly map finding origins so `clang-analyzer-*` diagnostics still behave like analyzer findings.
  - Preserve fallback candidate behavior.
  - Preserve final suppressed/in-scope finding counts.
  - Measure runtime under recorded `Worker jobs: N`.

## Implemented Result: Combined tidy/analyzer TU Pass

Date: 2026-05-16

Implementation:

- `run_tidy_check()` now detects adjacent `tidy-tu` and `analyzer-tu` clang-tidy TU passes and runs them as one combined clang-tidy invocation per compile command.
- The logical `ExecutionPlan.tidy_passes` remains unchanged, so dedupe priorities still know about both `tidy-tu` and `analyzer-tu`.
- `_parse_clang_tidy_output()` now accepts an optional origin resolver.
- Combined pass origin mapping:
  - diagnostics tagged with `clang-analyzer-*` keep origin `analyzer-tu`
  - normal tidy diagnostics keep origin `tidy-tu`
  - `clang-diagnostic-*` compiler diagnostics keep origin `tidy-tu`
- The combined checks string preserves the normal deep tidy checks and appends `clang-analyzer-*` without a second `-*` reset.
- Native fallback candidate behavior remains equivalent for the full run: the combined pass queued 170 source TUs for possible fallback, and later dedupe against findings reduced actual fallback execution to 165 TUs, matching the previous full validation's native-fallback count.

Focused validation:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS/source/database/config/Config.cc \
  --profile icts \
  --build-dir build \
  --jobs 16 \
  --kinds tidy \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail
```

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Exit code: 0
- Final in-scope findings: 0
- Final out-of-scope findings: 0
- Reported runtime total: 11.081s
- Runtime entries included:
  - `tidy-tu+analyzer-tu`: 7.855s, count 1
  - `tidy-tu+analyzer-tu:src/operation/iCTS/source/database/config/Config.cc`: 7.847s

Unit validation after implementation:

```bash
python3 -m py_compile .trellis/ecc_dev_tools/check.py .trellis/ecc_dev_tools/checkers.py .trellis/ecc_dev_tools/tests/test_core.py
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
```

Result:

- `py_compile` exit code: 0
- Unit tests exit code: 0
- 185 tests passed
- Added coverage verifies:
  - parser origin resolver maps mixed tidy/analyzer/compiler diagnostics correctly
  - `_combine_tidy_checks()` removes the secondary `-*` reset and preserves enabled globs
  - the clang-tidy TU runner applies the origin resolver
  - no-diagnostic TU output still queues native fallback candidates
  - `run_tidy_check()` records a `tidy-tu+analyzer-tu` phase and does not execute a separate adjacent `analyzer-tu` phase

Full validation with explicit jobs=16:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --jobs 16 \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Exit code: 0
- Wall time from `/usr/bin/time -p`: 437.29s
- Reported checker runtime: 435.624s
- Final in-scope findings after suppressions: 0
- Final out-of-scope findings after suppressions: 5119
- Notes record `Worker jobs: 16` for each check kind.

### Combined jobs=16 Runtime Comparison

| Metric | Baseline jobs=16 | After header parallelism | After combined TU pass | Delta vs baseline | Delta vs header parallelism |
| --- | ---: | ---: | ---: | ---: | ---: |
| Wall time | 569.365s | 474.420s | 437.290s | -132.075s (-23.2%) | -37.130s (-7.8%) |
| Reported checker runtime | 567.716s | 472.768s | 435.624s | -132.092s (-23.3%) | -37.145s (-7.9%) |
| `tidy` | 319.318s | 217.731s | 182.777s | -136.541s (-42.8%) | -34.953s (-16.1%) |
| TU tidy/analyzer work | 148.454s | 160.301s | 122.300s | -26.154s (-17.6%) | -38.001s (-23.7%) |
| `tidy-headers` | 125.820s | 10.742s | 11.465s | -114.355s (-90.9%) | +0.723s (+6.7%) |
| `iwyu` | 210.705s | 216.801s | 213.904s | +3.199s (+1.5%) | -2.897s (-1.3%) |
| `headers` | 34.805s | 35.375s | 36.027s | +1.222s (+3.5%) | +0.653s (+1.8%) |

Interpretation:

- The second slice produced an additional measurable runtime reduction without reducing coverage: full jobs=16 wall time improved from 474.420s after header parallelism to 437.290s with the combined TU pass.
- The combined TU phase cost 122.300s for all 191 compile commands, compared with 160.301s for the previous separate `tidy-tu` + `analyzer-tu` phases in the post-header-parallelism run.
- Final in-scope findings remained zero after suppressions, and final out-of-scope findings remained 5119.
- `tidy-headers` remains about 91% faster than baseline and is no longer the dominant tidy bottleneck.
- IWYU remains the largest non-tidy contributor and is still dominated by STA/SDC adapter dependency graph weight.

### Combined Pass Hotspots

Top combined `tidy-tu+analyzer-tu` unit entries:

| Runtime | File |
| ---: | --- |
| 40.933s | `src/operation/iCTS/test/flow/synthesis/TopologyTest.cc` |
| 40.825s | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc` |
| 37.391s | `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeSupport.cc` |
| 35.991s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc` |
| 35.868s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc` |
| 34.135s | `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` |
| 32.363s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` |
| 31.733s | `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechFrontierSupport.cc` |

Top final IWYU entries after the combined pass:

| Runtime | File |
| ---: | --- |
| 95.555s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc` |
| 91.983s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc` |
| 89.802s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` |
| 88.464s | `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` |
| 84.481s | `src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.cc` |
| 83.049s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterRootDriverQuery.cc` |
| 82.823s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharCircuit.cc` |
| 81.974s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCellQuery.cc` |
| 81.528s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterClockLookup.cc` |
| 81.461s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharTiming.cc` |

Final remaining optimization assessment:

- Low-risk checker scheduling wins in this task are now mostly exhausted under the user's no-shortcut constraint.
- The clearest remaining full-check bottleneck is IWYU over dependency-heavy STA/SDC adapter TUs. Line count alone still does not explain the hotspots: several small STA adapter files remain among the slowest IWYU units because they share broad external dependency graphs and compile include flags.
- Next runtime work should focus on dependency hygiene or adapter boundary cleanup, with before/after iCTS runtime evidence, rather than skipping files or disabling tools.

## Implemented Result: Header Self-Check Subcheck Queue

Date: 2026-05-16

Implementation:

- Header self-check planning now builds one `HeaderCheckPlan` per header.
- The direct self-contained header compile and the include-first wrapper compile are separate subcheck tasks.
- All direct and include-first subchecks run through one `_parallel_map()` queue using the same `snapshot.jobs` worker count.
- Coverage is unchanged: every scoped header still runs both subchecks.
- Findings are aggregated deterministically by header label, subcheck phase, and finding sort key.
- Runtime detail now distinguishes direct and include-first unit work:
  - direct: `src/.../Header.hh`
  - include-first: `include-first:src/.../Header.hh`

Focused validation:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.hh \
  --path src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh \
  --path src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.hh \
  --profile icts \
  --build-dir build \
  --jobs 16 \
  --kinds headers \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail
```

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Exit code: 0
- Runtime total: 0.719s
- `header-self-check`: 0.441s
- Findings: 2 in-scope findings, both for `STAAdapterInternal.hh`, preserving the same direct/include-first failure origins expected for this focused scope.
- Runtime detail included both direct labels and `include-first:<path>` labels.

Unit validation:

```bash
python3 -m py_compile .trellis/ecc_dev_tools/check.py .trellis/ecc_dev_tools/checkers.py .trellis/ecc_dev_tools/tests/test_core.py
python3 -m unittest discover -s .trellis/ecc_dev_tools/tests -v
```

Result:

- `py_compile` exit code: 0
- Unit tests exit code: 0
- 188 tests passed
- Added coverage verifies:
  - direct and include-first checks still run for every header
  - one task queue receives `2 * header_count` subchecks
  - out-of-order subcheck results still aggregate findings deterministically

Full validation with explicit jobs=16:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --jobs 16 \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Exit code: 0
- Wall time from `/usr/bin/time -p`: 417.53s
- Reported checker runtime: 415.878s
- Final in-scope findings after suppressions: 0
- Final out-of-scope findings after suppressions: 5119
- Notes record `Worker jobs: 16` for each check kind.

### Final jobs=16 Runtime Comparison

| Metric | Baseline jobs=16 | After combined TU pass | Final after header subcheck queue | Delta vs baseline | Delta vs combined pass |
| --- | ---: | ---: | ---: | ---: | ---: |
| Wall time | 569.365s | 437.290s | 417.530s | -151.835s (-26.7%) | -19.760s (-4.5%) |
| Reported checker runtime | 567.716s | 435.624s | 415.878s | -151.838s (-26.7%) | -19.746s (-4.5%) |
| `tidy` | 319.318s | 182.777s | 167.126s | -152.192s (-47.7%) | -15.651s (-8.6%) |
| `tidy-tu+analyzer-tu` | n/a | 122.300s | 111.722s | n/a | -10.578s (-8.6%) |
| `tidy-headers` | 125.820s | 11.465s | 10.403s | -115.417s (-91.7%) | -1.062s (-9.3%) |
| `headers` | 34.805s | 36.027s | 34.879s | +0.074s (+0.2%) | -1.148s (-3.2%) |
| `header-self-check` | 32.606s | 33.727s | 31.942s | -0.664s (-2.0%) | -1.785s (-5.3%) |
| `iwyu` | 210.705s | 213.904s | 211.100s | +0.395s (+0.2%) | -2.804s (-1.3%) |

Interpretation:

- The final full run is the best measured jobs=16 result for this task: 417.53s wall time, 26.7% faster than the 569.365s baseline.
- `tidy-headers` remains the largest direct win and is still about 91.7% faster than baseline.
- Header self-check task splitting gives a modest improvement inside the smaller `headers` phase.
- Some of the wall-time difference between the combined-pass run and final run is normal Clang tooling variability; the directly attributable header phase change is the `header-self-check` reduction from 33.727s to 31.942s.
- Final in-scope findings remain zero after suppressions, so the runtime reduction did not come from reducing check coverage.

### Final Runtime Distribution

| Check kind | Runtime | Share of reported runtime |
| --- | ---: | ---: |
| `iwyu` | 211.100s | 50.8% |
| `tidy` | 167.126s | 40.2% |
| `headers` | 34.879s | 8.4% |
| `cmake` | 2.542s | 0.6% |
| `format` | 0.231s | ~0.1% |

Final major phases:

| Phase | Runtime | Count |
| --- | ---: | ---: |
| `tidy-tu+analyzer-tu` | 111.722s | 191 |
| `tidy-headers` | 10.403s | 151 |
| `clang-frontend` | 32.122s | 191 |
| `native-fallback` | 12.697s | 165 |
| `header-self-check` | 31.942s | 151 |
| `dependency-scan` | 2.038s | 191 |

Top final IWYU entries remain STA/SDC adapter TUs:

| Runtime | File |
| ---: | --- |
| 89.179s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` |
| 89.054s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc` |
| 87.515s | `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc` |
| 86.444s | `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` |
| 83.871s | `src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.cc` |

## Rejected Experiment: Kind-Level Parallelism

Date: 2026-05-16

Question:

- Should `tidy`, `headers`, `cmake`, and `iwyu` run concurrently instead of serially by check kind?

Experiment:

- Run the heavy check kinds concurrently in one Python process.
- Use `jobs_per_kind=8` for the heavy parallel tool runners to reduce per-kind worker count.
- Preserve the same check kinds and full iCTS scope.

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Wall time from `/usr/bin/time -p`: 416.87s
- Measured wall inside the experiment script: 415.234s
- `tidy`: 366.905s
- `iwyu`: 415.221s
- `headers`: 66.594s
- `cmake`: 7.143s

Comparison against the final sequential-kind jobs=16 full run:

| Metric | Final sequential kinds | Concurrent kinds experiment |
| --- | ---: | ---: |
| Wall time | 417.53s | 416.87s |
| `tidy` | 167.126s | 366.905s |
| `iwyu` | 211.100s | 415.221s |
| `headers` | 34.879s | 66.594s |

Decision:

- Do not implement kind-level parallelism as default behavior.
- It overlaps work, but the overlap is almost exactly offset by resource contention.
- It also makes per-kind runtimes much worse and would make runtime diagnosis less stable.
- This is not a useful no-coverage-loss optimization on this machine.

## Rejected Experiment: Lower Full-Check Worker Count

Date: 2026-05-16

Question:

- Would running the final sequential-kind full check with fewer workers improve wall time by reducing Clang/IWYU contention?

Experiment:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check \
  --path src/operation/iCTS \
  --profile icts \
  --build-dir build \
  --jobs 8 \
  --output-format json \
  --no-fail-on-findings \
  --runtime-detail \
  --runtime-logging
```

Raw run artifacts were summarized into this analysis and intentionally removed before commit.

Result:

- Exit code: 0
- Wall time from `/usr/bin/time -p`: 689.89s
- Reported checker runtime: 688.262s
- Final in-scope findings after suppressions: 0
- Final out-of-scope findings after suppressions: 5119

Comparison against final `--jobs 16`:

| Metric | Final jobs=16 | Experiment jobs=8 |
| --- | ---: | ---: |
| Wall time | 417.53s | 689.89s |
| Reported checker runtime | 415.878s | 688.262s |
| `tidy` | 167.126s | 285.159s |
| `headers` | 34.879s | 54.735s |
| `iwyu` | 211.100s | 345.497s |

Decision:

- Do not lower the default or recommended full-check worker count based on this experiment.
- For this machine and workload, `--jobs 8` preserves findings but is materially slower than `--jobs 16`.

## Final Runner-Level Optimization Assessment

No further low-risk checker-runner optimization is obvious under the user's constraints:

- `tidy-headers` was the main serial bottleneck and is now parallelized.
- `tidy-tu` and `analyzer-tu` repeated Clang parsing and are now combined while preserving origin mapping and fallback behavior.
- Header self-check already had per-header parallelism; its direct/include-first subchecks now use a finer shared queue.
- IWYU is already parallelized over all scoped compile commands, and its slow STA/SDC adapter TUs are already near the front of the compile command order, so simple slow-file-first queue sorting would not materially shorten the IWYU tail.
- Kind-level parallelism was measured and rejected because it causes resource contention without meaningful wall-clock improvement.
- Lowering the final full-check worker count to 8 was measured and rejected because it increased wall time to 689.89s.
- Changed-file-only checks, skip lists, disabling analyzer/IWYU/header checks, broad suppressions, and altered IWYU semantics remain rejected because they reduce check coverage or quality.

The remaining major runtime issue is product-code dependency shape, especially STA/SDC adapter translation units with broad external iSTA/iDB/Liberty/Power include graphs. That is a separate dependency-hygiene/refactor task, not another checker scheduling fix.
