# Research: CTS evaluation units and metrics

- Query: Research CTS evaluation metrics and unit consistency. Inspect ClockTreeEvaluator, CTSAPI summaries, feature parser/database structs, and generated `scripts/design/ics55_dev/result/cts/cts.log`; determine misleading or unitless evaluation fields and recommend names/units.
- Scope: internal
- Date: 2026-04-27

## Findings

### Files Found

- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh` - declares the `ClockTreeSummary` fields exported by the evaluator.
- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc` - computes CTS evaluation metrics and emits `CTS Evaluation Summary`.
- `src/operation/iCTS/api/CTSAPI.hh` - exposes CTS flow and feature-summary API entry points.
- `src/operation/iCTS/api/CTSAPI.cc` - maps `ClockTreeSummary` to `ieda_feature::CTSSummary` and emits API runtime tables.
- `src/operation/iCTS/source/flow/FlowManager.cc` - resets/evaluates the evaluator and emits CTS flow status summaries.
- `src/feature/database/feature_icts.h` - defines the feature DB `CTSSummary` mirror of CTS evaluation fields.
- `src/feature/parser/feature_parser_tools.cpp` - serializes `CTSSummary` fields to JSON with current field names.
- `src/feature/database/feature_ieval.h` - defines broader evaluation summary structs; wirelength fields are raw `HPWL`, `FLUTE`, `HTree`, `VTree`, `GRWL` without unit suffix.
- `src/evaluation/database/wirelength_db.h` - defines the eval module wirelength summary structs with the same unitless wirelength names.
- `src/operation/iCTS/source/database/io/Wrapper.cc` - imports iDB coordinates directly into CTS `Point<int>` and exposes DBU-per-micron.
- `src/operation/iCTS/source/database/spatial/Point.hh` - generic point coordinate container used for CTS locations.
- `src/operation/iCTS/source/database/routing/SteinerTree.hh` - stores route-tree edge distances in the same coordinate type as the tree.
- `src/operation/iCTS/source/utils/geometry/Geometry.hh` - `Manhattan()` preserves caller coordinate semantics.
- `src/operation/iCTS/source/module/routing/Router.cc` - converts route edge DBU distances to microns before querying RC.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc` - converts DBU route distances to microns before installing STA RC.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterCellQuery.cc` - computes cell height in um and cell area in um^2.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` - computes clock timing and suggested frequency from ns-period timing data.
- `src/operation/iCTS/source/utils/logger/Schema.cc` - adds `elapsed_s` to scoped stage summaries.
- `src/utility/usage/usage.cc` - implements `Stats::elapsedRunTime()` and `Stats::memoryDelta()`.
- `scripts/design/ics55_dev/result/cts/cts.log` - generated log under analysis.

### Related Specs

- `.trellis/spec/backend/logging-guidelines.md:13-15` requires repository logging macros plus iCTS schema/report helpers for structured output such as `cts.log`.
- `.trellis/spec/backend/logging-guidelines.md:40-42` prefers titled schema tables/detail blocks for dense summaries and says field assembly should live near data owners.
- `.trellis/spec/project-constraints.md:64-65` repeats the requirement to use repository logging and iCTS schema/report helpers.
- `.trellis/spec/guides/cross-layer-thinking-guide.md:27-30` specifically asks cross-layer work to identify exact type and unit at each boundary.
- `.trellis/spec/guides/cross-layer-thinking-guide.md:46-48` lists DBU vs user-unit confusion as a common failure mode.

### Current CTS Evaluation Fields

`ClockTreeSummary` currently contains:

| Field | Declared Type | Current Source | Current Unit / Meaning |
|---|---:|---|---|
| `buffer_num` | `int32_t` | incremented for every buffer in `clock.get_insts()` | count of final CTS clock-member buffer instances |
| `buffer_area` | `double` | sum of `STAAdapter::queryCellAreaUm2()` for buffer cell masters | um^2, but field name and log omit the unit |
| `clock_path_min_buffer` | `int32_t` | derived from `collectBufferMembershipMetrics()` | count, but not a real source-to-sink path count |
| `clock_path_max_buffer` | `int32_t` | derived from `collectBufferMembershipMetrics()` | count, but not a real source-to-sink path count |
| `max_level_of_clock_tree` | `int32_t` | derived from `collectBufferMembershipMetrics()` | count of clock-member buffers, not real tree level/depth |
| `max_clock_wirelength` | `int32_t` | max of each evaluated net route-tree wirelength | DBU, but field name/log omit unit; actually per-net max |
| `total_clock_wirelength` | `double` | sum of each evaluated net route-tree wirelength | DBU, but field name/log omit unit |
| `clocks_timing[].setup_tns` | `double` | STA `getTNS(... kMax)` | ns implied by suffix |
| `clocks_timing[].setup_wns` | `double` | STA `getWNS(... kMax)` | ns implied by suffix |
| `clocks_timing[].hold_tns` | `double` | STA `getTNS(... kMin)` | ns implied by suffix |
| `clocks_timing[].hold_wns` | `double` | STA `getWNS(... kMin)` | ns implied by suffix |
| `clocks_timing[].suggest_freq` | `double` | `1000.0 / suggested_period_ns` | MHz by formula, but field name omits unit |

Code evidence:

- `ClockTreeEvaluator.hh:32-51` declares `ClockTreeSummary`, including the path/depth/wirelength names without unit suffixes.
- `ClockTreeEvaluator.cc:59-68` resets all evaluator fields.
- `ClockTreeEvaluator.cc:71-87` counts every buffer in `clock.get_insts()` and assigns that same count to `min_buffer_count`, `max_buffer_count`, and `max_level`.
- `ClockTreeEvaluator.cc:146-152` sums `std::max(edge.distance, edge.routed_distance)` to compute route wirelength.
- `ClockTreeEvaluator.cc:175-177` adds that wirelength into `total_clock_wirelength` and updates `max_clock_wirelength`.
- `ClockTreeEvaluator.cc:217-229` emits `CTS Evaluation Summary` with raw field names and values.
- `ClockTreeEvaluator.cc:251-258` increments `buffer_num` and adds `queryCellAreaUm2()` for each buffer.
- `STAAdapterCellQuery.cc:139-161` computes cell area by dividing DBU width and height by DBU-per-micron squared, so `buffer_area` is um^2.
- `STAAdapterTimingUpdate.cc:102-109` queries timing and computes `suggest_freq` as `1000.0 / suggested_period_ns`, so the practical unit is MHz.

### Misleading Evaluation Fields

`clock_path_min_buffer`, `clock_path_max_buffer`, and `max_level_of_clock_tree` are the most misleading fields.

The generated log reports:

| Field | Value |
|---|---:|
| `buffer_num` | `412` |
| `clock_path_min_buffer` | `412` |
| `clock_path_max_buffer` | `412` |
| `max_level_of_clock_tree` | `412` |

The matching log lines are `scripts/design/ics55_dev/result/cts/cts.log:705-718`. Those values are suspicious because the same log reports HTreeBuilder selected `levels = 6` and `selected_depth = 6` earlier (`cts.log:283-318`), while the evaluator reports a tree level of `412`.

The code explains the mismatch: `collectBufferMembershipMetrics()` does not traverse source-to-sink paths and does not compute graph depth. It counts all buffer instances in `clock.get_insts()` once, then uses that count as min path buffer count, max path buffer count, and max level (`ClockTreeEvaluator.cc:71-87`). For a single-clock run, these fields naturally equal `buffer_num`.

Recommended handling:

- If current semantics are kept, rename these fields to membership-count names and remove the duplicate/ambiguous level wording:
  - `clock_path_min_buffer` -> `min_clock_buffer_count`
  - `clock_path_max_buffer` -> `max_clock_buffer_count`
  - `max_level_of_clock_tree` -> remove, or `max_clock_buffer_count` if it remains the same metric
- If the intended metrics are real path/depth metrics, fix the computation and use:
  - `min_clock_path_buffer_count`
  - `max_clock_path_buffer_count`
  - `max_clock_tree_depth` or `max_clock_tree_level_count`
- Do not report the current `max_level_of_clock_tree` as a level/depth metric.

`max_clock_wirelength` is also misleading. It is not the maximum total wirelength per clock; it is the maximum wirelength of any evaluated clock net. The evaluator loops over the clock source net and each inserted/member net, updates the max for each net, and sums all nets (`ClockTreeEvaluator.cc:274-280`). Recommended names:

- `max_clock_wirelength` -> `max_clock_net_wirelength_dbu`
- `total_clock_wirelength` -> `total_clock_tree_wirelength_dbu`

`buffer_area` lacks units. It should be:

- `buffer_area` -> `buffer_area_um2` or `final_buffer_area_um2`

`suggest_freq` lacks units in the feature/timing structs and JSON. It should be:

- `suggest_freq` -> `suggest_freq_mhz`

### Wirelength Unit Consistency

CTS evaluator wirelengths are currently DBU.

Evidence:

- `Wrapper::idbToCts()` returns `{coord.get_x(), coord.get_y()}` with no conversion (`Wrapper.cc:261-264`).
- `Wrapper::queryDbUnit()` returns `get_micron_dbu()` from iDB units (`Wrapper.cc:134-140`).
- `Point<int>` carries raw integer coordinates (`Point.hh:28-42`).
- `geometry::Manhattan()` returns the same coordinate type and preserves caller distance semantics (`Geometry.hh:41-52`).
- `SteinerEdge<T>::distance` and `ClockSteinerEdge<T>::routed_distance` are stored in coordinate type `T` (`SteinerTree.hh:59-73`, `SteinerTree.hh:307-317`).
- The evaluator sums route-tree edge distances directly without dividing by DBU-per-micron (`ClockTreeEvaluator.cc:146-152`).
- RC conversion code does divide DBU by DBU-per-micron before querying RC (`Router.cc:73-85`, `STAAdapterRcTree.cc:139-154`), which confirms route-tree distances are DBU at that boundary.

The generated log is inconsistent:

- Topology tables often put DBU in the value text, e.g. `span_width_height = 479337 x 481389 DBU` and path lengths with `DBU` suffix (`cts.log:96-118`).
- Cluster distance summary uses field-name suffixes such as `min_distance_dbu` and `manhattan_distance_dbu` (`cts.log:331-345`).
- CTS Evaluation Summary emits `max_clock_wirelength` and `total_clock_wirelength` as bare numbers with no DBU suffix (`cts.log:705-718`).

Recommended policy for CTS evaluation and feature JSON:

- Machine-facing fields should carry unit suffixes in the field name and keep numeric values raw:
  - `max_clock_net_wirelength_dbu`
  - `total_clock_tree_wirelength_dbu`
  - `buffer_area_um2`
  - `suggest_freq_mhz`
  - `setup_tns`, `setup_wns`, `hold_tns`, `hold_wns` can stay because their names already contain timing unit suffixes.
- Human report tables may either keep the same suffixed field names or add a `Unit` column; avoid mixing value suffixes in one table with field suffixes in another for comparable metrics.
- Add `design_dbu_per_um` near the CTS run/evaluation context if DBU values remain user-visible. The feature layout summary has `design_dbu` (`feature_parser_summary.cpp:75`), but `cts.log` does not expose DBU-per-micron alongside CTS evaluation wirelengths.

### Runtime and Memory Unit Consistency

Runtime is currently represented in seconds, but two naming styles are emitted:

- API runtime tables use `elapsed_time_s`, e.g. `CTS API Evaluation Runtime` (`CTSAPI.cc:124-127`, `cts.log:720-725`).
- `schema::ScopedStage::finish()` appends `elapsed_s` to stage summaries (`Schema.cc:400-413`), creating nearby duplicate timing fields such as `CTSEvaluation Evaluate CTS clock tree Summary` (`cts.log:728-735`).

Both are seconds. Recommended naming:

- Prefer one field name across CTS reports: `elapsed_time_s`.
- If scoped stage summaries continue to emit `elapsed_s`, avoid separate adjacent API runtime tables unless memory is also needed there.

Memory is currently represented as `memory_delta_mb`, but the implementation is more specific than the name:

- `Stats::memoryUsage()` reads `/proc/<pid>/status` and uses `VmPeak`, not RSS (`usage.cc:42-74`).
- It multiplies the reported kB value by `1000`, then `memoryDelta()` multiplies the byte delta by `1e-6` (`usage.cc:82-87`), so the reported unit is decimal MB.
- Therefore `memory_delta_mb` is a peak virtual-memory delta from `Stats` construction, not current resident memory and not necessarily stage-local RSS.

Recommended naming:

- If keeping the current `Stats` implementation, rename `memory_delta_mb` to `peak_vmem_delta_mb` or `vm_peak_delta_mb`.
- If users expect resident-memory growth, change the underlying stat source separately and name it `rss_delta_mb`.
- Avoid bare `memory_delta_mb` in user-facing CTS summaries because it hides the peak-virtual-memory semantics.

### Feature Parser and Database Consistency

CTS feature output mirrors the misleading/unitless evaluator fields unchanged:

- `CTSAPI.cc:49-70` maps `ClockTreeSummary` directly to `ieda_feature::CTSSummary`.
- `feature_icts.h:25-35` defines `CTSSummary` with `buffer_area`, `max_clock_wirelength`, and `total_clock_wirelength` but no unit suffixes.
- `feature_parser_tools.cpp:286-314` serializes the same unitless/misleading names to JSON.
- `feature_ista.h:24-32` defines `ClockTiming` with `suggest_freq` but no unit suffix.

Broader evaluation wirelength structs also use unitless names:

- `feature_ieval.h:17-25` defines `TotalWLSummary` with `HPWL`, `FLUTE`, `HTree`, `VTree`, and `GRWL`.
- `wirelength_db.h:22-46` defines eval `TotalWLSummary`, `NetWLSummary`, and `PathWLSummary` with unitless wirelength names.
- `feature_parser_eval.cpp:129-141` serializes those wirelength fields directly.

For CTS feature output, the recommended stable names are:

| Current Feature Field | Recommended Field | Unit |
|---|---|---|
| `buffer_num` | `final_buffer_count` | count |
| `buffer_area` | `final_buffer_area_um2` | um^2 |
| `clock_path_min_buffer` | `min_clock_path_buffer_count` if recomputed; otherwise `min_clock_buffer_count` | count |
| `clock_path_max_buffer` | `max_clock_path_buffer_count` if recomputed; otherwise `max_clock_buffer_count` | count |
| `max_level_of_clock_tree` | `max_clock_tree_depth` or `max_clock_tree_level_count` if recomputed; otherwise remove | level/depth count |
| `max_clock_wirelength` | `max_clock_net_wirelength_dbu` | DBU |
| `total_clock_wirelength` | `total_clock_tree_wirelength_dbu` | DBU |
| `suggest_freq` | `suggest_freq_mhz` | MHz |

If backward compatibility matters, emit both old and new feature keys for one migration window, but mark old path/depth fields deprecated until their computation is corrected.

### Generated Log Findings

Relevant generated log values:

- Runtime configuration uses units in values: `skew_bound_ns = 0.0800 ns`, `max_cap_pf = 0.1500 pF`, `max_length_um = 400.0000 um`, `wire_width_um = library_default` (`cts.log:22-45`).
- Runtime RC uses units in values for `query_length_um`, `unit_resistance`, and `unit_capacitance` (`cts.log:47-57`).
- ReadData and API runtime tables use units in field names: `elapsed_time_s`, `memory_delta_mb` (`cts.log:67-85`).
- Cluster distance summary uses DBU suffixes in field names (`cts.log:331-345`).
- CTS Evaluation Summary lacks unit suffixes for `buffer_area`, `max_clock_wirelength`, and `total_clock_wirelength`, and reports misleading path/depth fields (`cts.log:705-718`).

The most actionable CTS Evaluation Summary replacement would be:

| Field | Unit / Type |
|---|---|
| `idb_writeback_done` | bool |
| `sta_timing_refreshed` | bool |
| `final_buffer_count` | count |
| `final_buffer_area_um2` | um^2 |
| `min_clock_path_buffer_count` | count, only after real path traversal |
| `max_clock_path_buffer_count` | count, only after real path traversal |
| `max_clock_tree_depth` | level/depth count, only after real topology/net traversal |
| `max_clock_net_wirelength_dbu` | DBU |
| `total_clock_tree_wirelength_dbu` | DBU |
| `design_dbu_per_um` | DBU/um |

## Caveats / Not Found

- I did not edit CTS code or rerun the flow; findings are based on static code inspection and the existing generated `cts.log`.
- I did not find any current evaluator traversal that computes real source-to-sink buffer counts or true clock-tree depth. The current implementation only counts clock-member buffers.
- I did not find unit metadata in `CTSSummary` itself; units are implicit in names or implementation.
- I did not use external references. Timing units for `suggest_freq` are inferred from local code using `getPeriodNs()` and `1000.0 / suggested_period_ns`.
- The broader eval wirelength structs use unitless names, but this research did not trace every eval API producer to prove whether all non-CTS wirelength values are DBU; the CTS evaluator wirelength path is DBU.
