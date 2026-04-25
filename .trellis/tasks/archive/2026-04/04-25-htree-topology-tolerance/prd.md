# HTree Topology Tolerance

## Goal

Introduce an `htree_topology_tolerance` runtime config knob for CTS H-tree topology construction. The knob defaults to `0.10` and limits how far each level's segment length may deviate from that level's baseline segment length, so topology embedding can stay closer to the actual load/leaf distribution without forcing every segment in the same level onto one exact baseline length.

## What I Already Know

- Current topology generation embeds child nodes from clustered load centers, then `balanceTopology` projects every child in a level onto an L1 circle using that level's average parent-child Manhattan distance.
- `BuildLevelPlans` later computes one average requested length per level and maps that baseline length to characterization length bins.
- Materialization places buffers along the actual parent-child topology edge by normalized segment pattern position, so any topology-level projection directly affects final H-tree leaf positions and buffer locations.
- Config is singleton-backed (`CONFIG_INST`) with JSON parsing, defaults in `Config::reset`, runtime report rows, and existing H-tree knobs such as `force_branch_buffer` and `htree_depth_explore_window`.

## Requirements

- Add config parameter `htree_topology_tolerance`.
- Default value must be `0.10`.
- Read the parameter from CTS config JSON via `CONFIG_INST`.
- Interpret the value as a per-level allowed relative deviation from the baseline segment length.
- For each level, allow segment lengths in `[baseline * (1 - tolerance), baseline * (1 + tolerance)]` rather than forcing all segments to exactly the baseline.
- Preserve existing behavior as much as possible when tolerance is `0.0`.
- Include the parameter in runtime config reporting.

## Acceptance Criteria

- [ ] `CONFIG_INST.get_htree_topology_tolerance()` returns `0.10` after reset/default initialization.
- [ ] JSON config `{ "htree_topology_tolerance": 0.25 }` overrides the default.
- [ ] Runtime config report includes `htree_topology_tolerance`.
- [ ] Topology balancing keeps every adjusted edge distance within the configured relative tolerance window for its level, subject to DBU rounding and bounding-box constraints.
- [ ] H-tree build/materialization continues to use the selected topology edge locations and characterization remains based on the per-level baseline/length bin plan.
- [ ] Existing CTS and topology tests continue to pass.

## Technical Approach

### Primary implementation path

1. Extend `Config` with `_htree_topology_tolerance`, getter, setter, default reset value, JSON parse entry, and runtime config report row.
2. In `TopologyGen::build`, pass tolerance into topology balancing, or let `balanceTopology` read `CONFIG_INST` if keeping signature churn minimal is preferred.
3. Replace exact level-radius projection in `balanceTopology` with tolerant projection:
   - compute the current level baseline as today: average parent-child Manhattan distance;
   - for each child edge, compute its current distance to parent;
   - if current distance is inside `[baseline * (1 - tol), baseline * (1 + tol)]`, keep the node position unchanged;
   - if shorter/longer, project toward the nearest boundary radius using existing `geometry::ProjectToL1Circle`;
   - clamp tolerance to a non-negative range in setter/parser.
4. Keep `BuildLevelPlans` unchanged for MVP: it continues to characterize and select segment patterns using one baseline length per level. The tolerance changes topology coordinates, not characterization bin selection.
5. Add focused unit coverage for config parsing/reporting and topology edge-distance behavior.

### Why this is preferred

- It fixes the root cause at topology embedding (`balanceTopology`) before final CTS object materialization.
- It preserves the existing characterization contract, avoiding a much larger change to per-edge/per-level segment pattern selection.
- It keeps tolerance semantics intuitive: `0.10` means each edge may stay within ±10% of the level baseline.

## Alternatives Considered

### A. Tolerant balancing only (recommended MVP)

- How: keep per-level baseline characterization, but avoid projecting already-good edges to exact baseline.
- Pros: low-risk, localized, preserves existing H-tree char/materialization pipeline.
- Cons: physical edge lengths can differ from the characterized baseline by up to tolerance, so timing RC is still approximated at level granularity.

### B. Per-edge characterization/bin selection

- How: each edge uses its own length bin/pattern, with topology pattern composition aware of per-edge lengths.
- Pros: best timing/physical fidelity.
- Cons: much broader change across level plans, segment frontier lookup, topology char composition, pattern materialization, and logging.

### C. Tolerance as a validation/report-only metric

- How: keep current exact projection, only report violations against actual load centers.
- Pros: safest mechanically.
- Cons: does not solve the leaf/load distance deviation problem.

## Out of Scope

- Redesigning H-tree characterization to support per-edge segment patterns.
- Changing clustering/partitioning algorithms.
- Changing sink clustering behavior or load grouping.
- Reworking actual-load legality filtering.

## Technical Notes

- `src/operation/iCTS/source/module/topology/TopologyGen.cc`: `build` calls `embedPositions` then `balanceTopology`; `balanceTopology` currently computes `avg_dist` and projects every node to that radius.
- `src/operation/iCTS/source/utils/geometry/Geometry.hh`: `ProjectToL1Circle` can be reused to project only out-of-window edges to lower/upper tolerance boundary.
- `src/operation/iCTS/source/flow/htree/HTreeLevelPlan.cc`: `BuildLevelPlans` computes per-level average requested length and aligned characterization bin.
- `src/operation/iCTS/source/flow/htree/HTreeMaterialization.cc`: materialization interpolates buffer locations along actual parent-child topology edges.
- `src/operation/iCTS/source/database/config/Config.hh` and `Config.cc`: location for default, parse, getter/setter, and runtime reporting.
- Likely tests: `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc` for config/report coverage; `src/operation/iCTS/test/module/topology/topology_gen/` for topology tolerance behavior.

## Decision Update

User confirmed the MVP direction on 2026-04-25: per-edge characterization is out of scope; characterization must still use the per-level baseline segment length. This implementation should only add tolerant topology balancing and config plumbing.

## Requirement Update: Real-Tech Comparison

User requested an arm9 full-sink H-tree real-tech comparison test for this task. The test should run the same arm9 full-sink H-tree scenario twice:

- default `htree_topology_tolerance` (`0.10`)
- legacy/exact behavior with `htree_topology_tolerance = 0.0`

The comparison should focus on:

- whether leaf-to-load movement/distance improves with the default tolerance;
- post-route/post-build STA result comparison, especially achieved skew for each setting.

This requirement keeps per-edge characterization out of scope; both runs must still characterize from per-level baseline segment length.

## Design Correction

User clarified that `TopologyGen` should receive topology tolerance through `BiPartitionConfig` rather than reading `CONFIG_INST` directly. Keep `CONFIG_INST` as the runtime JSON source, but translate it at the CTS flow boundary when constructing `HTreeBuilder::BuildOptions`; then `HTreeBuilder` transfers the option into `BiPartitionConfig`. This preserves module layering and keeps topology generation reusable/config-explicit.

## BuildOptions Correction

User clarified that `htree_topology_tolerance` should also be available as an `HTreeBuilder::BuildOptions` field, defaulting to `std::nullopt`; when unset, `HTreeBuilder` uses the `BiPartitionConfig` default of `0.10`. `HTreeBuilder` must not directly read `CONFIG_INST` for this topology parameter.

## Test Correction

The first comparison report mixed a topology path-length proxy with the requested post-STA skew. That is misleading because `ClockSynthesis::build` currently materializes iCTS-owned objects but does not commit them back into the STA timing graph. The real-tech test should not fabricate STA skew from topology path length. Until a CTS-object-to-STA writeback path exists, the comparison artifact should report leaf/load paired movement metrics and explicitly mark post-STA skew as unavailable.

## STA Comparison Correction

User clarified the required post-analysis method:

- For each synthesized CTS net, build an STA RC tree from routing RC / geometry-derived RC.
- Query arrival time through STA interfaces at the sink/load pins.
- STA skew is `max(arrival_time) - min(arrival_time)`.
- Wirelength skew should use the same max-min definition over source-to-load routed/path wirelengths.
- Do not use topology path skew as a substitute for STA skew.
