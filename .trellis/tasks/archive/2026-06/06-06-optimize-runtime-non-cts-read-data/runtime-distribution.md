# Runtime Distribution Analysis

## Scope

This report analyzes the runtime distribution for:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The goal is to explain why the current CTS phase is dominated by early
`CTSReadData`, and to identify optimization directions outside CTS synthesis
algorithm code.

## Artifacts

- Current baseline:
  `.trellis/tasks/06-06-optimize-runtime-non-cts-read-data/artifacts/baseline_perf/`
- Captured files:
  - `run.stdout`
  - `run.stderr`
  - `cts.log`
  - `cts_detail.log`
  - `iCTS_metrics.json`
  - `perf.data`
  - `perf_report_children.txt`
  - `perf_report_no_children.txt`
  - `perf_hotspots.txt`

The baseline command succeeded. `/usr/bin/time -v` reported `47.13 s` wall
time for the perf-wrapped full script, `44.38 s` user time, `2.61 s` system
time, and maximum RSS `5,472,960 KB`.

## CTS Runtime Distribution

Current `cts.log` reports CTS internal runtime:

| Stage | Seconds | Percent of CTS Total | Peak VMem Delta |
| --- | ---: | ---: | ---: |
| read_data | 32.656 | 74.42% | 1668.352 MB |
| synthesis | 10.470 | 23.86% | 3441.480 MB |
| optimization | 0.592 | 1.35% | 0.000 MB |
| instantiation | 0.097 | 0.22% | 0.000 MB |
| evaluation | 0.054 | 0.12% | 0.000 MB |
| total | 43.883 | 100.00% | 5109.832 MB |

`cts_report` is emitted after CTS run completion and is separately timed at
`0.309 s`; it is not part of the `43.883 s` CTS Runtime Overview total.

## ReadData Breakdown From Timestamps

The aggregate `read_data` timer does not yet expose sub-metrics, but the glog
timestamps in `run.stderr` isolate the dominant work:

| Substep | Start | End | Seconds | Percent of ReadData |
| --- | --- | --- | ---: | ---: |
| CTSReadData aggregate | 13:08:17.572345 | 13:08:50.228044 | 32.655699 | 100.00% |
| SDC parse until parsed log | 13:08:17.572345 | 13:08:17.572417 | 0.000072 | ~0.00% |
| H7CL Liberty load | 13:08:17.573202 | 13:08:20.310498 | 2.737296 | 8.38% |
| H7CL Liberty link | 13:08:20.310513 | 13:08:33.849632 | 13.539119 | 41.46% |
| H7CR Liberty load | 13:08:33.849826 | 13:08:36.636415 | 2.786589 | 8.53% |
| H7CR Liberty link | 13:08:36.636433 | 13:08:50.089310 | 13.452877 | 41.20% |
| Post-Liberty trace/materialize/report | 13:08:50.089550 | 13:08:50.228044 | 0.138494 | 0.42% |

Combined Liberty parser/link time is approximately `32.516 s`, or `99.57%` of
the current `CTSReadData` runtime. Liberty link alone is about `26.992 s`, or
`82.65%` of `CTSReadData` and `61.51%` of CTS total runtime.

## Perf Evidence

`perf record -F 49 -g --call-graph fp` captured 2284 samples.

Children-mode report shows:

| Symbol | Children | Interpretation |
| --- | ---: | --- |
| `icts::Wrapper::loadLibertyIfNeeded` | 60.32% | Dominant full-flow sampled path |
| `ista::LibertyReader::linkLib` | 50.97% | Dominant work inside Liberty loading |
| `ista::LibertyReader::visitPin` | 38.67% | Major link/parse visitor |
| `ista::LibertyReader::visitInternalPower` | 21.26% | Major internal-power table visitor |

Kernel symbol resolution was restricted, but user-space iEDA symbols resolved
well enough for the relevant finding.

## Code Path

`Flow::readClockData()` starts the `read_data` runtime metric and the
`CTSReadData` stage, then delegates to `ClockDataRead::read(...)`.

`ClockDataRead::read(...)` currently:

1. Parses SDC clock data through `SdcClockReader().readClockData(...)`.
2. Calls `wrapper.traceSdcClocks(...)`.
3. Calls `wrapper.readTraceClockTargets(...)` for accepted targets.

`Wrapper::traceSdcClocks(...)` builds a Liberty-cell lookup callback:

```cpp
const SdcLibertyCellLookup liberty_cell_lookup =
    [this](const std::string& cell_master) -> ista::LibCell* {
      return findLibertyCell(cell_master);
    };
```

`Wrapper::findLibertyCell(...)` calls `loadLibertyIfNeeded()`.
`loadLibertyIfNeeded()` iterates every configured lib path, parses it with
`lib.loadLibertyWithCppParser(...)`, links it with `reader.linkLib()`, and
builds `_lib_cell_by_master`.

That explains the wall-clock sequence: the first Liberty-backed clock-trace
lookup lazily triggers full Liberty parsing/linking inside `CTSReadData`.

## Historical Comparison

An archived May artifact from
`.trellis/tasks/archive/2026-05/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p2_strict_pre_comp_gate_enabled/time.txt`
shows a different distribution:

| Stage | May Artifact | Current Baseline |
| --- | ---: | ---: |
| read_data | 8.053 s | 32.656 s |
| synthesis | 31.169 s | 10.470 s |
| evaluation | 7.589 s | 0.054 s |
| total | 46.826 s | 43.883 s |

The May `read_data` log shows `Sta.cc: read sdc clock periods only`, not the
current `LibParserCpp` load/link sequence. The current slowdown is therefore a
path change caused by SDC clock tracing and wrapper Liberty queries, not random
noise in the same path.

## Conclusion

The current CTS runtime is not dominated by CTS synthesis. It is dominated by
iSTA Liberty parsing/linking triggered during `CTSReadData`.

The actual clock tracing and iDB-to-CTS materialization after Liberty is loaded
take roughly `0.14 s` in this run. The expensive part is loading and especially
linking two full Liberty files. The task scope has therefore shifted to iSTA
Liberty reader/linker optimization; CTS remains only the workload entry and will
not be edited for this task.

## Optimization Candidates

### 1. iSTA Liberty Visitor Dispatch Fast Path

Replace per-call local `std::map<std::function>` dispatch tables in
`LibertyReader::visitGroup()`, `visitSimpleAttri()`, and
`visitComplexAttri()` with allocation-free direct dispatch.

Expected impact: high. This targets the measured link path directly and removes
repeated map construction, `std::bind`, capturing lambda/function-wrapper
creation, double lookup, and copied `std::function` invocation from every group
or attribute visit.

Risk: medium. `visitSimpleAttri()` has many branches, so the implementation must
preserve every existing conversion/free/action branch exactly.

### 2. iSTA Pin Name Fast Path

Avoid regex matching in `LibertyReader::visitPin()` for the common scalar-pin
case. Only run range parsing when the name actually contains a bus/range shape.

Expected impact: medium-high. perf shows `visitPin()` as the largest child
symbol under `LibertyReader::linkLib`.

Risk: low-medium. The change must preserve current behavior for range pins such
as `A[3:0]` and any unsupported/edge pin forms.

### 3. iSTA Statement Traversal / Table Parsing Cleanup

After re-measuring candidates 1 and 2, consider optimizing
`visitStmtInGroup()`'s two-pass traversal or `visitAxisOrValues()` table-value
allocation/parsing.

Expected impact: uncertain before removing dispatch overhead.

Risk: medium-high because traversal order and table parsing are more
semantics-sensitive.

### 4. Diagnostics / Cache / Config Changes

CTS substep metrics, shared Liberty cache, or library-list reduction remain
possible follow-up directions, but they are not the first implementation target
for this task because the user scope is iSTA parser/linker runtime and no CTS
source edits.

## Recommended Next Step

Start with candidate 1:

1. Keep the implementation local to iSTA Liberty reader/linker code.
2. Replace the dynamic visitor dispatch pattern with behavior-equivalent direct
   dispatch.
3. Validate iSTA Liberty functionality, re-run `ics55_dev`, and compare
   `read_data` plus Liberty link timestamps against the saved baseline.
4. If `visitPin()` remains a top hotspot, apply the pin-name fast path as the
   second slice.

## Implemented Result

The first two candidates were implemented in iSTA Liberty parser/linker code:

- Dynamic visitor dispatch tables were replaced in `visitGroup()`,
  `visitSimpleAttri()`, and `visitComplexAttri()`.
- `visitPin()` now skips regex matching for scalar pin names.

Post-change final result:

| Metric | Baseline | Final |
| --- | ---: | ---: |
| read_data | 32.656 s | 10.469 s |
| Liberty link total | 26.992 s | 4.682 s |
| CTS total | 43.883 s | 21.674 s |

Tracked iCTS metrics remained identical to the baseline. See
`post-change-runtime.md` for the full comparison.
