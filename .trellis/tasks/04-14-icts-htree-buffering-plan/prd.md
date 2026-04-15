# brainstorm: refactor iCTS sink clustering and H-tree buffering

## Goal

Refactor the current iCTS routing topology flow to remove level-dependent clustering heuristics, stabilize sink-side clustering, replace buffer-level recursive topology generation with a top-down balanced H-tree style construction, and add a deterministic level-uniform buffer sizing pass that optimizes end-to-end PPA. The immediate motivation is to remove the current non-convergent behavior and reduce sensitivity to per-level tuning knobs.

## What I already know

* The current debug entry is [run_iCTS_dev.tcl](/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl), which calls `run_cts -config $CTS_CONFIG -work_dir $CTS_WORK_DIR`.
* The current CTS config is [cts_default_config.json](/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json).
* The current runtime flow is `readData() -> routing() -> evaluate()` in [CTSAPI.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/api/CTSAPI.cc).
* Each clock net enters `Router::routing()` and then `Solver::run()`, which currently executes `init() -> resolveSinks() -> breakLongWire() -> levelReport()` in [Solver.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/solver/Solver.cc).
* The current sink resolution loop is driven by `while (cur_pins.size() > 1)` in [Solver.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/solver/Solver.cc).
* The current observed failure mode is a non-terminating 2-pin loop seen in [CTS.log](/nfs/share/home/huangzhipeng/code-new/ecc-benchmark/runs/20260414_133623/ics55_00008/CTS_ecc/log/CTS.log): once the flow reaches two pins, the next level also stays at two pins forever.
* The immediate trigger is that `iterClustering()` special-cases `load_pins.size() == 2` into one cluster, but `slackClustering()` can split it back into two singleton clusters when the estimated net length exceeds the configured max length. That keeps `cur_pins.size()` constant and the loop never exits.
* Current `level_*` config arrays are interpreted with "reuse the last value forever" semantics in [CtsConfig.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/data_manager/config/CtsConfig.hh), which makes deep levels highly sensitive to the last configured constraint.
* Current `use_skew_tree_alg = OFF` means the active topology path is not a skew-tree path. The code falls back to `shiftCBSTree() -> defaultTree() -> shallowLightTree()` in [TreeBuilder.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/solver/tools/tree_builder/TreeBuilder.cc).

## Problem Statement

The current iCTS solver mixes three unstable ideas in one loop:

* repeated sink reduction driven by per-level config arrays,
* length-based reclustering (`slackClustering`) after initial MCF + k-means grouping,
* guide-point shifting and tree-building heuristics that can keep the number of "next-level" pins unchanged.

This creates two practical issues:

* convergence depends on a fragile interaction between `level_max_length`, `level_max_fanout`, `shift_level`, latency-opt levels, and tree-estimated net length;
* topology construction is hard to reason about, because sink clustering, guide shifting, buffer placement, and topology generation are all entangled.

The requested redesign should separate responsibilities:

* sink stage: stable one-shot leaf clustering only,
* buffer stage: balanced recursive topology only,
* sizing stage: explicit level-based buffer master enumeration only.

## User-Requested Hard Requirements

### 1. Remove `level_*` configs and use defaults only

* Remove all `level_*` configuration fields from the active CTS flow.
* The refactored flow must rely only on global/default constraints such as `max_fanout`, `max_cap`, `max_length`, `skew_bound`, and the configured `buffer_type` list.
* No deep-level tail reuse behavior may remain in the new flow.

### 2. Sink layer keeps MCF + k-means, but removes shift-based heuristics

* Sink clustering should still use the current MCF + k-means family of algorithms.
* Sink clustering should not use `shift_*` driven guide relocation.
* Sink clustering should use the sink cluster centroid as the leaf-buffer location seed.
* The sink stage should produce leaf buffer instances that become the inputs to the buffer-level topology stage.

### 3. Buffer-level topology becomes a top-down H-tree style flow

* The topology over buffer instances should be constructed recursively by balanced binary clustering.
* Given a set of buffer instances, use k-means + MCF with target 50/50 split and allow 10% flow imbalance.
* Each recursive split should use the child cluster centroid as the branch-point location.
* The recursion should continue until the cluster size reaches one leaf buffer.

### 4. Buffering policy

* Only branch points and leaf nodes should use inserted buffers.
* For every branch point, insert a real buffer instance.
* For the sink stage, each sink cluster should terminate at a real leaf buffer instance.

### 5. Level-uniform sizing enumeration

* All buffers on the same topology depth must use the same cell master.
* Higher levels must use size greater than or equal to lower levels.
* The flow should enumerate legal level-wise sizing assignments and choose the final buffering from the normalized delay/area/power Pareto frontier.
* The optimization objective must account for:
  * root-to-leaf delay,
  * total buffer area,
  * total buffer power.
* The final selected solution should be the most balanced Pareto-optimal point rather than a lexicographic single-metric optimum.

### 6. Root buffer participates in the same sizing search

* The H-tree root buffer must always be materialized and connected from the source to the top H-tree root.
* `root_buffer_type` is removed from the active config surface.
* The root buffer must not use a dedicated fixed master.
* The root buffer must participate in the same depth-uniform sizing assignment as every other inserted buffer.

### 7. Long-wire buffering is always enabled

* `break_long_wire` is removed from the active config surface and treated as always enabled.
* `min_buffering_length` is the only active knob for the post-topology long-wire buffering pass.
* After the initial H-tree is built, if any parent-to-child edge exceeds `min_buffering_length`, the flow must insert evenly spaced buffers so that every resulting segment length is no greater than the threshold.
* All inserted break buffers on an edge must inherit the sizing level of the original child buffer on that edge.

### 8. Candidate feasibility must be restored before Pareto selection

* The sizing search must evaluate all legal monotone assignments without dropping any feasible solution.
* Pareto normalization and balanced-front selection must run on the feasible candidate set only.
* The active candidate-feasibility checks must cover:
  * skew bound,
  * buffer transition,
  * sink transition,
  * active max capacitance,
  * active max net length,
  * active max fanout.
* Any search acceleration must preserve the feasible set and final selected solution quality exactly.

## Assumptions (Temporary but Actionable)

* "Remove `shift_*` parameters" is interpreted as removing `shift_level` and the centroid-shift behavior currently implemented in `Solver::netAssign()`.
* The sink stage becomes a one-shot leaf clustering stage rather than a recursive "reduce until one pin remains" stage.
* `slackClustering()` is removed from the active sink clustering path because it is a direct source of non-monotonic cluster-count behavior.
* The requested "H-tree" is interpreted as a balanced recursive binary tree over buffer sets using k-means + MCF splits and centroid branch points. This is an H-tree style topology, not a strict classical axis-symmetric geometric H-tree.
* MVP objective selection is interpreted as:
  * enumerate all feasible level-size assignments,
  * compute raw delay/area/power for each assignment,
  * min-max normalize each metric across the feasible set,
  * compute the Pareto frontier in normalized 3D metric space,
  * choose the Pareto-optimal point with the smallest Euclidean distance to the ideal point `(0, 0, 0)`,
  * break ties by smaller `max(delay_norm, area_norm, power_norm)`,
  * break remaining ties by smaller sum of normalized metrics.
* The 10% split tolerance will be hard-coded in the first version rather than exposed as a new config field.
* Existing dead/unused config keys such as `router_type`, `cluster_type`, `delay_type`, `scale_size`, `cluster_size`, `external_model`, `root_buffer_type`, `level_*`, and `shift_level` are not part of the active flow and must only survive as deprecated-warning inputs.
* Search acceleration is limited to quality-preserving changes such as avoiding storage of infeasible candidates, reducing repeated library copies, and adding summary counters; no heuristic pruning of feasible assignments is allowed.

## Open Questions

* Whether strict skew constraints should remain part of the first H-tree MVP or be measured only in reports while topology is redesigned.
* Whether power should be computed from liberty leakage only, or from leakage plus switching/internal components when those models are available.
* Whether root buffering remains disabled by default, or whether the first branch level should optionally absorb current `root_buffer_required` behavior.

## Requirements (Evolving into Implementation Contracts)

### Functional Requirements

* The flow must eliminate the current recursive sink-reduction loop that depends on `level_*` arrays.
* The flow must generate leaf buffers from sink clusters in one stable pass.
* The flow must build a recursive balanced binary branch topology over leaf buffers.
* The flow must materialize actual buffer instances at every branch node and leaf node.
* The flow must assign one uniform buffer master per depth level.
* The flow must evaluate all legal non-increasing level-size assignments from the configured `buffer_type` list.
* The flow must select one final level-size assignment using a deterministic normalized Pareto-front objective.

### Non-Functional Requirements

* The new flow must guarantee termination for every clock net.
* The topology construction path must be easier to inspect than the current mixed sink/tree/shift loop.
* The implementation should preserve the current Tcl and tool-manager entrypoints (`run_cts`, `reportCTS`) so the outer flow remains stable.
* The generated result should be explainable with per-net and per-level reports.

## Proposed Architecture

### Stage A: Input and global constraints

Keep the existing outer flow:

* `readData()`
* `routing()`
* `evaluate()`

But inside `routing()` replace the current solver internals with three explicit phases:

1. `buildSinkLeafBuffers()`
2. `buildBalancedBufferTree()`
3. `enumerateLevelSizingAndCommit()`

### Stage B: Sink clustering to leaf buffers

Input:

* source driver pin
* sink pins only

Algorithm:

1. Cluster sinks with MCF + k-means using global constraints:
   * target fanout from `max_fanout`
   * cap guidance from `max_cap`
   * length guidance from `max_length`
2. Do not run `slackClustering()`.
3. For each sink cluster:
   * compute centroid,
   * create one leaf buffer instance at centroid,
   * legalize the location,
   * connect leaf buffer to the sinks in that cluster.

Output:

* a set of leaf buffer instances
* one leaf net per sink cluster

### Stage C: Top-down balanced H-tree over leaf buffers

Input:

* source pin
* all leaf buffer instances

Recursive function:

* `buildHSubtree(parent_node, buffer_instances, depth)`

Rules:

* If `buffer_instances.size() == 1`, connect `parent_node` directly to that leaf buffer and stop.
* Otherwise:
  * run 2-way k-means + MCF on `buffer_instances`,
  * target an ideal 50/50 split,
  * enforce allowed child cardinality range of `[floor(0.4 * n), ceil(0.6 * n)]`,
  * compute centroid for each child cluster,
  * create one branch buffer at each centroid,
  * connect `parent_node` to the two branch buffers,
  * recurse on each child cluster.

Notes:

* The recursion order is top-down even though cluster membership is decided from the set of descendant leaf buffers.
* No non-buffer Steiner-only branch node should remain in the committed topology.

### Stage D: Level-uniform sizing enumeration and Pareto selection

Input:

* branch/leaf buffer topology with topology depths fixed
* ordered `buffer_type` list from the config

Definitions:

* Depth 0 is the highest inserted buffer level under the clock source.
* Larger-drive choices are considered "higher or equal size".
* All nodes at the same depth share one cell master.

Enumeration space:

* Generate all non-increasing level-size vectors:
  * `size[0] >= size[1] >= ... >= size[max_depth]`
* The candidate masters come from `buffer_type`, sorted by drive strength.

Evaluation per candidate:

1. Assign the chosen master to every branch/leaf buffer by depth.
2. Propagate timing through the whole clock tree.
3. Compute:
   * worst root-to-leaf delay,
   * total inserted buffer area,
   * total inserted buffer power.
4. Store the raw metrics for the full feasible candidate set.

Normalization and selection:

1. Min-max normalize delay, area, and power independently across the feasible candidate set.
2. Compute the Pareto frontier over the normalized 3D metric vectors.
3. Select the "most balanced" Pareto-optimal point by:
   * minimizing Euclidean distance to the ideal point `(0, 0, 0)`,
   * tie-break by minimizing the worst normalized component,
   * tie-break again by minimizing the sum of normalized metrics.

Commit:

* After enumeration, materialize only the selected Pareto-balanced sizing assignment into the design and reports.

## Research Notes

### What the current code does

* Current sink reduction is recursive and level-based inside [Solver.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/solver/Solver.cc).
* Current clustering stack is:
  * `iterClustering()` for initial MCF + k-means grouping,
  * `slackClustering()` for recursive length-based re-splitting,
  * optional `clusteringEnhancement()` by annealing.
* Current tree generation path under `use_skew_tree_alg = OFF` eventually uses `defaultTree()` and `shallowLightTree()` in [TreeBuilder.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/solver/tools/tree_builder/TreeBuilder.cc).
* Current config parsing for per-level constraints happens in [JsonParser.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/data_manager/config/JsonParser.cc) and is stored in [CtsConfig.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/data_manager/config/CtsConfig.hh).

### Why this redesign is technically justified

* The current convergence bug is not just bad tuning; it is a structural non-termination risk caused by recursive re-splitting without monotonicity guarantees.
* Moving sink clustering to a one-shot leaf-generation step removes the "same pin-count forever" class of failures.
* Building branch topology over leaf buffers is easier to reason about than repeatedly re-clustering mixed sink/buffer fronts.
* Level-uniform sizing naturally matches the requested monotone "high level >= low level" contract and is simpler to audit than per-instance greedy sizing.

## Impacted Files (Expected)

### Config and interface

* [scripts/design/ics55_dev/iEDA_config/cts_default_config.json](/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json)
* [src/interface/default_config/cts_default_config.json](/home/liweiguo/project/ecc-tools-dev/src/interface/default_config/cts_default_config.json)
* [src/interface/tcl/tcl_icts/tcl_ctsconfig.cpp](/home/liweiguo/project/ecc-tools-dev/src/interface/tcl/tcl_icts/tcl_ctsconfig.cpp)
* [src/operation/iCTS/source/data_manager/config/CtsConfig.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/data_manager/config/CtsConfig.hh)
* [src/operation/iCTS/source/data_manager/config/JsonParser.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/data_manager/config/JsonParser.cc)

### Core CTS flow

* [src/operation/iCTS/source/solver/Solver.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/solver/Solver.cc)
* [src/operation/iCTS/source/solver/Solver.hh](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/solver/Solver.hh)
* [src/operation/iCTS/source/module/router/Router.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/module/router/Router.cc)
* [src/operation/iCTS/source/solver/tools/tree_builder/TreeBuilder.cc](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/solver/tools/tree_builder/TreeBuilder.cc)

### Likely new modules

* `src/operation/iCTS/source/solver/tools/h_tree/` or equivalent new helper module
* `src/operation/iCTS/source/solver/tools/sizing/` or equivalent helper for level-wise sizing enumeration

## Implementation Plan

### Phase 1: Baseline instrumentation and guardrails

* Add explicit per-net debug reports for:
  * sink count,
  * leaf cluster count,
  * H-tree depth,
  * per-level node counts,
  * chosen level-size vector.
* Add a solver invariant: no active loop may continue if the frontier cardinality does not decrease or the recursion input does not shrink.

### Phase 2: Config surface simplification

* Remove all `level_*` fields from:
  * active JSON configs,
  * `CtsConfig`,
  * JSON parser,
  * Tcl config alteration surface.
* Remove `shift_*` driven topology behavior from the active flow.
* Remove `root_buffer_type` from active config parsing and active Tcl/config templates.
* Keep deprecated warning coverage for removed keys, including `root_buffer_type`, `level_*`, `shift_level`, and the other deleted legacy knobs.
* Keep only global constraints needed by the new sink stage and sizing stage.

### Phase 3: Sink-stage refactor

* Replace `resolveSinks()` recursive reduction with a leaf-buffer construction stage.
* Keep `iterClustering()` as the initial cluster engine.
* Remove `slackClustering()` from the active sink clustering path.
* Replace guide-point shifting with centroid-based leaf buffer placement.

### Phase 4: H-tree topology builder

* Add a dedicated recursive builder for balanced binary buffer topology.
* Implement 2-way k-means + MCF split with 10% imbalance tolerance.
* Create branch buffers at child centroids and recurse until each child cluster holds one leaf buffer.

### Phase 5: Level-size enumeration and Pareto optimization

* Build a depth-indexed list of branch/leaf buffers.
* Sort configured buffer masters by drive strength.
* Enumerate all non-increasing level-size vectors.
* Evaluate each candidate on timing, area, and power.
* Reject infeasible candidates only after full constraint evaluation; do not prune feasible candidates heuristically.
* Normalize metrics across the feasible candidate set.
* Build the Pareto frontier and select the most balanced frontier point.
* Select and commit the final assignment.

### Phase 6: Driver / GDS / logging contract cleanup

* Unify clock-driver resolution inside iCTS so router, evaluator, and GDS generation use one consistent rule.
* Remove duplicated ad-hoc driver fallback logic where possible and guard null-instance accesses everywhere they remain observable.
* Make the configured GDS path correspond to the actual design GDS output, and derive the flyline GDS as its sibling output.
* Preserve the existing RC / pin / instance summary logs while adding:
  * fatal exit on negative RC,
  * warning on zero-pin / single-pin nets,
  * explicit warning when deprecated config keys are parsed,
  * explicit terminal warning if the CTS log file cannot be opened.

### Phase 7: Reporting and validation

* Extend CTS reports with:
  * per-level buffer count,
  * per-level cell master,
  * worst root-to-leaf delay,
  * total inserted area,
  * total inserted power.
* Add regression coverage for:
  * no infinite loop on the current ics55 case,
  * stable topology generation with odd/even leaf counts,
  * monotone level sizing contract,
  * same-depth uniform sizing contract.

## Acceptance Criteria (Evolving)

* [ ] No `level_*` fields are needed in the active CTS config path.
* [ ] `root_buffer_type` is not part of the active CTS config path.
* [ ] The current ics55 CTS case terminates without the previous 2-pin infinite loop.
* [ ] Sink clustering produces leaf buffers using centroid placement without shift-based guide relocation.
* [ ] Buffer-level topology is constructed by recursive balanced binary clustering over buffer instances.
* [ ] Only branch points and leaf nodes materialize as inserted buffers.
* [ ] The root buffer is always kept and sized by the same level-wise sizing assignment as the rest of the tree.
* [ ] Every same-depth buffer uses the same master in the committed solution.
* [ ] Higher-depth ordering obeys the monotone size rule: upper level size is greater than or equal to lower level size.
* [ ] Long-wire buffering is always enabled and controlled only by `min_buffering_length`.
* [ ] Pareto selection runs on the feasible candidate set only.
* [ ] Deprecated config keys emit warnings in both terminal logging and the CTS log file when the log file is available.
* [ ] The configured design GDS path matches the actual emitted design GDS output path.
* [ ] The final chosen sizing is selected from the normalized delay/area/power Pareto frontier.
* [ ] The final chosen sizing corresponds to the most balanced Pareto-optimal point under the documented tie-break rules.
* [ ] CTS reports expose the final topology depth, per-level counts, per-level masters, and chosen PPA metrics.

## Validation Matrix

* Termination:
  * replay the current problematic arm9/ics55 CTS case and confirm finite completion.
* Structural correctness:
  * verify binary branching at every non-leaf branch node.
* Partition correctness:
  * confirm each recursive split stays inside the 50% +/- 10% child-count tolerance.
  * confirm small-N splits choose the nearest legal balanced partition instead of a looser rounded bound.
* Sizing correctness:
  * verify same-level uniformity and monotone top-down master ordering.
  * verify the root buffer follows the selected level-0 master rather than a dedicated config master.
* PPA selection:
  * confirm normalization is computed correctly across the feasible candidate set.
  * confirm the Pareto frontier is identified correctly.
  * confirm the chosen candidate matches the documented "most balanced" frontier rule.
  * confirm infeasible candidates are excluded before normalization.
* Config / IO correctness:
  * verify deprecated keys produce warnings but do not affect the active flow.
  * verify design GDS and flyline GDS land at the documented output paths.
  * verify log open failure produces an explicit terminal warning instead of a null dereference.

## Risks and Mitigations

* Risk: the requested "H-tree" semantics are underspecified compared with a strict geometric H-tree.
  * Mitigation: implement the user-specified recursive binary centroid tree as the canonical MVP and document it clearly.
* Risk: removing `slackClustering()` may increase some local wirelength.
  * Mitigation: make wirelength an observable report metric and rely on buffer-level topology + sizing to recover timing/PPA.
* Risk: exhaustive level-size enumeration may grow too large on deep trees.
  * Mitigation: exploit the monotone ordering to define the legal space, avoid repeated library copies, and keep only feasible candidates; do not add heuristic pruning of feasible assignments.
* Risk: normalized Pareto selection depends on the evaluated candidate set, so pruning must not distort the frontier.
  * Mitigation: do not prune feasible assignments; only reject candidates after explicit feasibility checks and log evaluated vs feasible counts.
* Risk: driver fallback inconsistencies can create null dereferences or mismatched reports/GDS.
  * Mitigation: centralize on one iCTS-side driver resolution rule and reuse it across router, evaluator, and GDS code.
* Risk: config/output-path drift can silently break downstream inspection.
  * Mitigation: keep one design-GDS contract, derive the flyline GDS as a sibling output, and warn on every deprecated key that is ignored.
* Risk: power data quality may vary by liberty coverage.
  * Mitigation: define one explicit power source hierarchy and log which values are used.
* Risk: current flow code mixes sink reduction and topology synthesis deeply inside `Solver`.
  * Mitigation: refactor to explicit named phases with dedicated helpers instead of patching more heuristics into the current loop.

## Out of Scope (Explicit)

* Re-implementing a mathematically exact classical symmetric H-tree geometry.
* Retaining backward-compatible behavior for `level_*` tuning.
* Preserving current `shiftCBSTree()` / `shallowLightTree()` as the main topology path for the new flow.
* GUI or Python feature work beyond whatever is needed to keep existing entrypoints compiling.
* Broad cleanup of all historical dead CTS config keys unless directly needed for this refactor.

## Definition of Done (Team Quality Bar)

* Code compiles and the new topology/sizing flow is wired into the existing `run_cts` entrypoint.
* The ics55 debug case no longer exhibits non-termination.
* Regression coverage exists for the new sink-stage and H-tree builder behavior.
* Reports expose enough structure to debug partitioning and sizing decisions.
* Spec/docs are updated to describe the new solver phases and config surface.
