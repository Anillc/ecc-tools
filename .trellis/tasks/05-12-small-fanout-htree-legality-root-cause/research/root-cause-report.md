# small-fanout H-tree legality root-cause report

## Scope

This report covers the `max_fanout = 4` failure reproduced through the dev flow:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The flow reads:

```text
/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json
```

The current config is:

```json
"max_fanout": "4",
"wirelength_iterations": "3",
"slew_steps": "10",
"cap_steps": "10"
```

## Initial Failure

The failing run was not a checkout or config-path mix-up. The dev `run_iCTS_dev.tcl` script hard-codes the `ecc-tools-dev` workspace and reads the dev config.

Initial failing facts from `scripts/design/ics55_dev/result/cts/cts.log`:

| item | value |
|---|---:|
| configured max fanout | 4 |
| root loads | 8751 |
| H-tree loads after sink clustering | 3010 |
| local cluster buffers | 3010 |
| generated H-tree depth | 11 |
| generated leaf count | 2048 |

Topology generation and frontier construction succeeded, but sink-load-region filtering removed every candidate. The failure ended with:

```text
reason = no_legal_depth_candidates
failed_clocks = 1
```

For the deepest explored depth, there were hundreds of thousands of raw candidates, but zero post-filter candidates:

| depth | raw frontier entries | feasible raw entries | post-filter candidates | post-filter feasible |
|---:|---:|---:|---:|---:|
| 11 | 697140 | 371632 | 0 | 0 |
| 10 | 476812 | 253940 | 0 | 0 |
| 9 | 415982 | 222182 | 0 | 0 |
| 8 | 332461 | 177593 | 0 | 0 |

## Root Cause

The root cause is a mismatch between where fanout is enforced and where H-tree load groups are formed.

The upstream sink clustering path correctly uses `CONFIG_INST.get_max_fanout()` and produces legal local-buffer clusters. With `max_fanout = 4`, it reduces 8751 original sinks into 3010 local-buffer input pins used as H-tree loads. That stage is not violating fanout.

The downstream H-tree topology embedding previously split those 3010 clustered H-tree loads geometrically using ratio-based bisection capacity. It did not cap each subtree by the number of leaves under that subtree multiplied by the configured fanout. As a result, a final boundary group under the bottom-most H-tree buffers could contain more than 4 clustered local-buffer loads even when every upstream sink cluster was legal.

Sink-load-region legality then applied the same `max_fanout` again at the boundary group level:

```text
loads->size() > max_fanout
```

That check is a hard monotone failure for the bottom-most buffered level. Because every candidate at each explored depth shared boundary groups that exceeded the small fanout, all legal candidates were removed late, after expensive frontier generation.

## User Question 1: Is clustering making small fanout too tight?

Answer: yes, clustering contributes structurally, but the clustering algorithm itself is not the fanout violator.

The important detail is abstraction. Sink clustering enforces `max_fanout = 4` over original sink pins and creates local buffers. H-tree then sees those local-buffer inputs as its loads. The previous H-tree embedding did not ensure that each H-tree leaf/boundary group would contain at most 4 of those clustered loads. Therefore `max_fanout = 4` was effectively applied once inside sink clustering and then again to groups of clustered loads near the bottom of the H-tree.

So the problematic behavior is not "fast clustering ignores fanout." The problem is that the leaf fanout-relative H-tree load grouping was not fanout-aware after clustering changed the load abstraction.

## User Question 2: Do intermediate H-tree levels consider fanout?

Answer: before the fix, not explicitly in topology/frontier construction. After the fix, both topology load distribution and pattern composition carry fanout state.

Before the fix, the H-tree frontier state contained electrical/lattice state and boundary semantic state, but not fanout/group-size state. Segment pattern metadata and topology pattern composition did not carry an explicit per-level fanout budget. Fanout was enforced in `SinkLoadRegion.cc` after pattern frontiers were already built and filtered for electrical feasibility.

The implemented fix adds fanout capacity to topology embedding, so the load distribution that feeds later H-tree construction is fanout-aware at each bisection step. It also adds `source_exposed_load_count` to the pattern composition state and frontier keys. Topology pattern composition now rejects unbuffered binary branch compositions whose exposed source fanout would exceed `max_fanout`, resets exposed fanout to `1` when an upstream source buffer is present, and applies the same binary fanout legality to the root closure because the selected root net drives two symmetric downstream branches. This prevents frontier pruning from collapsing legal low-fanout patterns into otherwise similar electrical states.

This handles the observed bottom boundary failure and the intermediate unbuffered-source fanout case for the current binary H-tree topology. If future topology variants allow non-binary branching or multiple local loads per intermediate driver, the same explicit fanout state should remain part of the frontier contract.

## Fix Implemented

Functional changes:

- `HTree::build()` now passes `CONFIG_INST.get_max_fanout()` into topology partition config as `max_leaf_load_count`.
- `TopologyGen::embedPositions()` converts the leaf fanout budget into a subtree load cap: `child_leaf_need * max_leaf_load_count`.
- `Clustering::biPartition()` honors an optional `max_cluster_size` when assigning loads during min-cost-flow bisection.
- H-tree topology pattern composition now tracks `source_exposed_load_count` in pattern state and frontier keys.
- Branch composition rejects unbuffered binary source fanout above `max_fanout`.
- Root fanout legality rejects candidates where the two downstream exposed branches together would exceed `max_fanout`.

This makes each recursive topology split respect the maximum number of H-tree loads that can legally fit under the available leaves of that child subtree.

Follow-on robustness fix:

- After the H-tree topology fix, the flow reached commit/routing and exposed a degenerate FLUTE case with overlapping terminal locations.
- `Router::buildFluteTree()` now legalizes overlapping driver/load or load/load terminal coordinates before calling FLUTE.
- `Router::buildClockNetTree()` keeps the simple validated clock star tree only for single-load nets.

## Hypotheses

| hypothesis | result | evidence |
|---|---|---|
| `max_fanout = 4` is below what the old topology embedding could satisfy | confirmed | old run generated raw frontiers but zero post-filter entries at depths 11, 10, 9, and 8 |
| Sink clustering creates legal local buffers but increases downstream boundary fanout pressure | confirmed | 8751 sinks became 3010 H-tree local-buffer loads; old H-tree grouping did not account for leaf fanout capacity |
| The leaf fanout-relative algorithm is too tightly coupled to clustered H-tree loads | confirmed with nuance | clustering is legal; downstream leaf/boundary grouping failed to reserve capacity for clustered loads |
| Intermediate H-tree construction does not enforce fanout early | confirmed before fix; fixed for current binary topology | old frontier state and pattern metadata did not include fanout/group-size dimensions; new `source_exposed_load_count` state preserves and prunes fanout legality during topology composition |
| Monotone hard-fail pruning is the primary bug | rejected as primary | monotone pruning amplified late rejection, but the root cause was illegal boundary groups from fanout-unaware topology embedding |
| Depth exploration window is the primary bug | rejected as primary | generated depth 11 with 2048 leaves is theoretically enough for 3010 clustered loads at fanout 4; the embedding distribution was the blocker |
| The legality model applies fanout at a suspicious abstraction boundary | partially confirmed | applying fanout to clustered H-tree loads is defensible, but the topology builder must then reserve matching leaf capacity |

## Final Acceptance Evidence

Latest final flow log:

```text
scripts/design/ics55_dev/result/cts/cts.log
```

Runtime config in the log:

```text
wirelength_iterations = 3
slew_steps = 10
cap_steps = 10
max_fanout = 4
```

The selected-depth H-tree candidate pool now survives sink-load-region filtering:

```text
candidate_frontier_entries = 60330
feasible_raw_entries = 51324
feasible_frontier_entries = 36180
global_feasible_refs = 36180
global_candidate_refs = 60330
selected_from = strict_feasible
legal = true
failure_reason = none
```

Selected topology:

```text
selected_depth = 11
inserted_insts = 1381
inserted_nets = 1381
htree_load_group_count = 2048
selected_physical_root_load = 0.0850 pF, terminals=4
selected_terminal_branch_buffered_levels = 4/11
used_boundary_fallback = false
```

CTS internal status:

```text
status = finished
finished_clocks = 1
failed_clocks = 0
```

Runtime:

```text
read_data     8.440s
synthesis     50.850s
instantiation 0.034s
evaluation    8.423s
report        2.047s
total         67.755s
```

Key results:

```text
selected_htree_depth = 11
htree_inserted_buffer_count = 1381
final_clock_buffer_count = 4392
total_clock_network_wirelength = 59190.091 um
elapsed_time = 67.755 s
peak_vmem_delta = 6841.660 MB
```

STA fanout evidence from the same final run:

```text
setup skew report nets with fanout > 4: 0
hold skew report nets with fanout > 4: 0
```

An intermediate post-fix run with a direct root exposed-count check selected `terminals=8` at root closure and left `cts_flow_clk_0_core_clock_regular_downstream_net` at fanout 8 in both setup and hold skew reports. The final root filter uses binary source fanout legality, reducing the selected physical root closure to `terminals=4` and eliminating the STA fanout violation.

Focused post-fix checks, both passed:

```text
ninja -C build iEDA
ninja -C build icts_test_module_routing
./bin/icts_test_module_routing --gtest_filter=RouterClockTreeTest.BuildFluteClockTreeLegalizesOverlappingTerminals:RouterClockTreeTest.BuildFluteClockTreePreservesTerminalMetadataAndRCTreeCap
```

## Baseline Fanout-32 Regression

Before finish-work, the dev config was temporarily changed to:

```text
max_fanout = 32
wirelength_iterations = 3
slew_steps = 10
cap_steps = 10
```

The same binary flow was rerun:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

CTS finished successfully:

```text
status = finished
failed_clocks = 0
selected_htree_depth = 5
htree_inserted_buffer_count = 40
final_clock_buffer_count = 360
total_clock_network_wirelength = 43561.302 um
elapsed_time = 28.501 s
```

The exact final metrics match the earlier `iter3_step10` / `max_fanout = 32` sweep baseline for the user-facing QoR fields:

```text
buffer_num = 360
total_clock_wirelength = 43561302 DBU
setup_wns = 7.308504 ns
hold_wns = 0.025084 ns
```

The setup and hold skew reports both had zero nets above fanout 32. The temporary config change was restored to `max_fanout = 4` after this regression run.

## Validation Notes

No broad `ecc dev` / `ecc_dev_tools` checks were used during debugging. Validation was based on targeted build plus the exact dev flow requested by the user, and CTS internal success was checked from `cts.log` rather than process exit code alone.

After the requested `max_fanout = 32` baseline regression, final `ecc_dev_tools` validation was run:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result:

```text
format: in-scope findings 0
tidy: in-scope findings 0
headers: in-scope findings 0
cmake: in-scope findings 0
iwyu: in-scope findings 0
overall: in-scope findings 0
```

The default dev config file under `scripts/design/ics55_dev/iEDA_config/cts_default_config.json` is ignored by git because the repository ignores `scripts/`. The on-disk config is correct for the requested flow, but it will not appear in `git diff`.
