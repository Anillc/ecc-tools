# CTS Log Analysis

## Source

* Log path: `scripts/design/ics55_dev/result/cts/cts.log`
* Generated at: `2026-04-27 12:03:14`
* Full flow runtime: `24.424735 s`
* Full flow memory delta: `4060.744000 MB`
* Outcome: `success`

## High-Level Flow

The log is structured as a sequence of ASCII tables:

1. Run context and runtime paths.
2. Runtime configuration and wire RC probing.
3. Clock distribution and read-data runtime.
4. Topology generation.
5. HTreeBuilder diagnostics and characterization grid planning.
6. CharBuilder initialization and sweep statistics.
7. HTreeBuilder selected topology and build metrics.
8. Cluster center to H-tree leaf distance summary and details.
9. CTS flow, evaluation, and final API runtime summaries.

## Key Observations

### Runtime Configuration

* One clock domain: `clk_i`.
* Total sinks: `8751`, all flip-flop sinks.
* `wirelength_unit` is `auto`, which later triggers an HTreeBuilder fallback.
* `max_fanout` is `32`.
* Routing layer configuration is reported as `5`, with configured order `5, 6`.
* Buffer candidates are `BUFX8H7L`, `BUFX12H7L`, `BUFX16H7L`, `BUFX20H7L`.

### Topology Generation

* The topology generation load count is `319`, not `8751`; this appears to be post-clustering load count.
* The generated H-tree has `511` nodes, depth `8`, and `256` leaves.
* Root-to-leaf path length ranges from `482876 DBU` to `586971 DBU`, average `532785.33 DBU`.

### Characterization Grid

* `wirelength_unit` falls back to `17.2512 um`.
* Requested level lengths: `8`.
* Required covering iterations: `8`.
* Effective direct characterization iterations are capped at `3`.
* Distinct level bins are `5`.
* The cap means the direct characterization grid does not cover all topology level lengths directly; later logic likely relies on aligned bins or fallback/lookup behavior.

### CharBuilder Sweep

* Generated segment characterizations: `4095`.
* Generated patterns: `87`.
* Executed STA samples by wirelength:
  * `17.2512 um`: `1110`
  * `34.5025 um`: `4260`
  * `51.7537 um`: `14160`
* Output slew overflow samples: `15435`.
* Max observed output slew: `0.2956 ns`.
* Configured max slew: `0.0500 ns`.
* Driven cap overflow samples: `45`.
* Max observed driven cap: `0.1568 pF`.
* Configured max cap: `0.1500 pF`.

### HTreeBuilder Build

* Selected H-tree levels: `6`.
* Depth candidates evaluated: `4`.
* Feasible solutions: `18280`.
* Inserted CTS buffer instances: `92`.
* Inserted CTS nets: `92`.
* Selected root driver: `BUFX20H7L`.
* Boundary fallback was not used.
* Selected pattern power: `300.767 uW`.
* Selected pattern delay: `0.2846 ns`.

### Cluster Center vs H-Tree Leaf Distance

The summary table says:

* Cluster count: `319`.
* Minimum distance: `1207 DBU`.
* Maximum distance: `128305 DBU`.
* Mean distance: `31718.22 DBU`.
* Median distance: `29367 DBU`.

Derived from the details table:

* Total clustered sinks: `8751`, matching the clock distribution.
* Sink count per cluster ranges from `15` to `32`.
* Distance percentiles:
  * p50: `29367 DBU`
  * p90: `51701 DBU`
  * p95: `57666 DBU`
  * p99: `69323 DBU`
* Outliers:
  * `37` clusters exceed `50000 DBU`.
  * `2` clusters exceed `80000 DBU`.
  * `2` clusters exceed `100000 DBU`.
* Worst clusters:
  * cluster `187`: `128305 DBU`
  * cluster `181`: `127346 DBU`
  * cluster `249`: `75618 DBU`
  * cluster `182`: `70104 DBU`
  * cluster `36`: `69323 DBU`

### Final Flow And Evaluation

* CTS flow succeeds for one clock and one regular sink group.
* CTS flow runtime: `13.627140 s`, memory delta `864.184000 MB`.
* CTS evaluation runtime: `6.728100 s`, memory delta `3196.560000 MB`.
* Evaluation reports:
  * `buffer_num = 412`
  * `buffer_area = 1376.480000`
  * `clock_path_min_buffer = 412`
  * `clock_path_max_buffer = 412`
  * `max_level_of_clock_tree = 412`
  * `max_clock_wirelength = 579914`
  * `total_clock_wirelength = 44114218.000000`

## Suspicious Or Confusing Items

1. `max_level_of_clock_tree = 412` looks suspicious because HTreeBuilder selected `levels = 6`, while evaluation reports a level count equal to `buffer_num`.
   Source inspection confirms this is caused by `collectBufferMembershipMetrics()` in `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc`, which currently counts all buffers in `clock.get_insts()` and assigns that count to path min/max and max level metrics.
2. `clock_path_min_buffer = 412` and `clock_path_max_buffer = 412` are suspicious for the same reason: these fields are currently buffer-membership totals, not real source-to-sink path buffer counts.
3. HTreeBuilder reports `inserted_insts = 92`, but evaluation reports `buffer_num = 412`. That may be valid if evaluation counts all buffers in the final clock network, including pre-existing or sink-cluster buffers, but the log does not explain the delta.
4. The 319-row cluster distance detail table is too verbose for the normal log and lacks ranking/filtering.
5. Characterization overflow counts are large, but the log does not tell the user whether these are expected, harmless clamping events, or quality concerns.
6. Units are inconsistent or under-explained: some tables use DBU, some use um/ns/pF, and some wirelength fields do not state whether they are DBU or um in the field name.

## Improvement Ideas

### P0 / High Value

* Add a top-level "Key Results" table near the end: outcome, clock count, sink count, inserted CTS buffers, final buffer count, max tree depth, max/total clock wirelength, worst cluster distance, overflow counts, runtime, memory.
* Add explicit metric definitions for evaluation fields, especially `clock_path_min_buffer`, `clock_path_max_buffer`, `max_level_of_clock_tree`, `max_clock_wirelength`, and `total_clock_wirelength`.
* Fix or rename suspicious evaluation metrics if they are not actually path depth metrics.
* Add a reconciliation table for buffer counts:
  `HTree inserted buffers`, `cluster/sink buffers`, `pre-existing clock buffers`, `evaluation final buffers`, and `delta explanation`.

### P1 / Debuggability

* Replace the full 319-row cluster detail dump in normal logs with a summary plus top-N worst clusters.
* Emit the full cluster detail to a separate debug artifact, such as `cluster_leaf_distance.csv`, or behind a verbose/debug option.
* Add percentiles to the cluster distance summary: p90, p95, p99, and count over thresholds.
* Add characterization overflow ratios, not just counts. For example:
  `output_slew_overflow_samples / executed_sta_samples`.
* Add a severity classification for overflow metrics: `ok`, `warning`, or `critical`, with threshold rationale.

### P2 / Readability

* Normalize numeric formatting: fewer trailing zeros for memory/time where precision is not useful; consistent decimal places for ns/pF/um.
* Add units directly to field names where values are raw numbers, especially DBU wirelength fields.
* Use consistent terminology for `levels`, `depth`, `level_count`, `clock tree level`, and `buffer path depth`.
* Separate "configuration", "algorithm decision", "quality result", and "debug detail" sections so users can scan logs faster.
