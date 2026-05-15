# Research: analytical h-tree solver options

- Query: analytical h-tree solver options for replacing native enumeration/pruning with affine or quadratic fitted models in `slew_in` and `cap_load`
- Scope: mixed
- Date: 2026-05-14

## Findings

### Summary recommendation

The safest path is not a full one-shot replacement of native enumeration on the first implementation. The current H-tree flow uses discrete pattern metadata as an executable contract for embedding, root-driver compensation, sink-load legality, branch-buffer forcing, fanout legality, and reporting. An analytical solver should initially sit under `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver/` as a candidate-ranking or candidate-shortlisting stage that still returns a selected `HTreeTopologyPattern`, `HTreeTopologyChar`, and `LevelPlan` sequence compatible with the existing embedding and reporting path.

Recommended implementation shape:

1. Keep `RunCharacterizationFlow`, `BuildLevelPlans`, `ResolveDepthCandidates`, `RootDriverCompensationPass`, `ResolveSinkLoadRegionLegality`, and `BuildEmbedding` as integration boundaries.
2. Fit per-segment-pattern surrogate models from existing `SegmentChar` samples over `(input_slew_ns, load_cap_pf)`.
3. Use an analytical optimizer to rank a small set of legal per-level segment-pattern sequences, then validate the chosen sequence through the existing discrete/root/sink legality path before embedding.
4. Fall back to native topology pruning if the fitted model is out of domain, infeasible, numerically unstable, or fails discrete validation.

A full continuous optimization that directly chooses arbitrary buffer positions/cell masters is higher risk because the embedding layer needs concrete `BufferingPattern` positions and cell masters, and the legality passes depend on `PatternId` materialization.

### Files found

- `src/operation/iCTS/source/flow/synthesis/htree/HTree.hh`: public H-tree entry, build options, selected level metadata, root-driver report, and build result surface.
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`: native H-tree orchestration from topology generation through characterization, segment frontier synthesis, depth search, selection, root-driver sizing, embedding, and summary logging.
- `src/operation/iCTS/source/flow/synthesis/htree/characterization/Characterization.hh`: characterization-grid flow contract and result type.
- `src/operation/iCTS/source/flow/synthesis/htree/characterization/Characterization.cc`: collection of requested level lengths, adaptive characterization-grid setup, and `CharBuilder` result capture.
- `src/operation/iCTS/source/flow/synthesis/htree/constraint/Constraint.hh`: boundary constraints used by search, currently `force_branch_buffer` and top input slew coverage.
- `src/operation/iCTS/source/flow/synthesis/htree/constraint/Constraint.cc`: conversion from build options to covering slew lattice indices.
- `src/operation/iCTS/source/flow/synthesis/htree/plan/Plan.hh`: level-plan and depth-candidate contracts.
- `src/operation/iCTS/source/flow/synthesis/htree/plan/Plan.cc`: level-length alignment and depth-candidate selection.
- `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.hh`: depth-search result and candidate-evaluation contracts.
- `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.cc`: per-depth evaluation loop and global feasible/candidate pool construction.
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentPruning.hh`: segment frontier synthesis contract.
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentPruning.cc`: native segment frontier closure by length, terminal kind, and state-pruned composition.
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentLibrary.hh`: segment and topology pattern metadata, frontier catalogs, combiners, and materialization helpers.
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.hh`: topology candidate assembly, filtering, Pareto compression, and selection contracts.
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`: native H-tree frontier composition, root fanout filtering, sink-load filtering, boundary filtering, and global selection policy.
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.hh`: root-driver direct compensation options, result detail, stats, and boundary-closure contracts.
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`: direct root-driver compensation, strict root boundary closure, and compensation caching.
- `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.hh`: sink-load-region legality contracts and capacitance distribution stats.
- `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc`: real sink-load legality, load-cap covering index resolution, and monotone failure caching.
- `src/operation/iCTS/source/database/characterization/CharCore.hh`: common electrical-boundary and cost carrier for segment/topology characterization.
- `src/operation/iCTS/source/database/characterization/SegmentChar.hh`: segment characterization and serial composition semantics.
- `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh`: H-tree topology characterization and binary fanout composition semantics.
- `src/operation/iCTS/source/database/characterization/ValueLattice.hh`: uniform lattice helper for slew/cap/length discretization.
- `src/operation/iCTS/source/database/characterization/BufferingPattern.hh`: concrete buffer positions, cell masters, terminal-branch-buffer flag, and monotonic composition rule.
- `src/operation/iCTS/source/module/characterization/CharBuilder.hh`: characterization options, grids, sample outputs, and segment-characterization storage.
- `src/operation/iCTS/source/module/characterization/CharBuilderPatternEnumeration.cc`: native enumeration of buffer topology slots and monotonic buffer-type combinations.
- `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc`: STA/PA sampling over input slew and load cap, producing `SegmentChar` entries.
- `src/operation/iCTS/source/module/characterization/CharBuilderSampleStorage.cc`: conversion of physical output slew and driven cap to lattice indices.
- `src/operation/iCTS/source/module/characterization/HTreeTraits.hh`: binary H-tree hash-join key relation, including half-load-cap matching.
- `src/operation/iCTS/source/module/characterization/SegmentTraits.hh`: serial segment hash-join key relation.
- `src/operation/iCTS/source/module/characterization/Frontier.hh`: state-frontier grouping and delay/power dominance pruning.
- `src/operation/iCTS/source/module/characterization/HashJoinEngine.hh`: generic discrete hash-join composition engine.
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc`: unit tests for degenerate behavior, segment-frontier requests, and selection behavior.
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeTest.cc`: real-tech smoke coverage for successful build, artifacts, logs, selected pattern, and load distribution.
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc`: real-tech regression coverage for branch-buffer forcing and top-boundary fallback.
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechMatrixSupport.cc`: real-tech matrix runner recording runtime, frontier count, depth, best delay/power, grid adaptation, and fallback.

No existing `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver/` directory was found. The requested path is therefore a proposed new submodule boundary, not an existing implementation.

### Related specs

- `.trellis/workflow.md`: research is a persisted planning artifact and sub-agent dispatch prompts must start from the active task path.
- `.trellis/spec/project-constraints.md`: iCTS code must use `.hh`/`.cc`, no exceptions, `LOG_*` logging, schema/report helpers for structured output, and CMake updates before adding modules.
- `.trellis/spec/backend/directory-structure.md`: `source/flow/synthesis/htree/` owns H-tree implementation; helper submodules live below that tree, and new modules require CMake targets plus parent `add_subdirectory`.
- `.trellis/spec/backend/quality-guidelines.md`: use established CTS terms, avoid broad snapshots of queryable state, use explicit target dependencies, keep headers self-contained.
- `.trellis/spec/backend/error-handling.md`: recoverable analytical-solver failures should log and return safe fallback/failure details, not throw.
- `.trellis/spec/guides/cross-layer-thinking-guide.md`: analytical solver crosses characterization, topology search, compensation, region legality, and embedding boundaries; units and ownership need explicit handling.
- `.trellis/spec/guides/code-reuse-thinking-guide.md`: reuse `UniformValueLattice`, `CharBuilder`, `SegmentChar`, `HTreeTopologyChar`, `BufferingPattern`, existing legality passes, and existing CMake target patterns.

### Existing native pipeline and contracts

`HTree::build` is the single public flow entry. It records `BuildOptions` including `force_branch_buffer`, `min_top_input_slew_ns`, target/depth-window controls, optional fixed topology root, shared characterization library, root-driver sizing, local-buffer topology mode, clock period, and logging context (`src/operation/iCTS/source/flow/synthesis/htree/HTree.hh:57`). The `BuildResult` is already broad enough to carry an analytical result if the solver returns the same core objects: `topology`, `levels`, `best_char`, `best_pattern`, characterization grid stats, selected depth/frontier counts, root compensation report, fallback markers, inserted design objects, and root pins/nets (`src/operation/iCTS/source/flow/synthesis/htree/HTree.hh:139`).

The native orchestration order is:

- Validate root driver/load preconditions and return safe failure reasons for missing driver or empty load set (`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:213`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:221`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:227`).
- Generate the physical topology with `TopologyGen::build`, using max fanout, H-tree tolerance, optional fixed root, DBU conversion, and local-buffer/sink load-count kind (`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:236`).
- Run characterization and obtain `CharBuilder` data (`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:260`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:269`).
- Resolve boundary constraints and level/depth plans (`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:271`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:278`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:285`).
- Build a `BufferPatternLibrary` from `CharBuilder` patterns and synthesize segment frontiers for required length indices (`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:294`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:299`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:310`).
- Configure root-driver compensation and per-depth fanout pruning, then search topology depth candidates (`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:323`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:333`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:345`).
- Filter global feasible/candidate pools by sink-load-region coverage, build per-depth delay/power Pareto refs, and select a strict-feasible or fallback global entry (`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:358`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:383`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:393`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:413`).
- Resolve selected sink-load legality, root-driver compensation, materialize `best_pattern`, annotate `LevelPlan`s, validate root-driver sizing, build embedding, and emit summary (`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:431`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:463`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:516`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:519`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:525`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:551`).

Characterization is already the correct data source for fitted models. `CharBuilder::InitOptions` controls wirelength, max slew/cap, buffer types, redundancy, slew/cap steps, routing layer, and wire width (`src/operation/iCTS/source/module/characterization/CharBuilder.hh:59`). `CharBuilder` exposes segment chars, buffering patterns, physical wirelengths, length/slew/cap lattices, and overflow counters (`src/operation/iCTS/source/module/characterization/CharBuilder.hh:80`). During sampling, each feasible topology is sampled across load caps and input slews, and stored as `SegmentChar` with input slew index, output slew index, driven cap index, load cap index, delay, power, and source-boundary switching power (`src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc:45`, `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc:78`, `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc:99`, `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc:106`). Out-of-lattice output slew/driven-cap samples are skipped (`src/operation/iCTS/source/module/characterization/CharBuilderSampleStorage.cc:32`, `src/operation/iCTS/source/module/characterization/CharBuilderSampleStorage.cc:45`, `src/operation/iCTS/source/module/characterization/CharBuilderSampleStorage.cc:50`).

The native electrical composition contracts are discrete and important:

- `CharCore` stores boundary buckets and cost metrics: input slew, output slew, driven cap, load cap, delay, power, pattern ID, and source-boundary net switch power (`src/operation/iCTS/source/database/characterization/CharCore.hh:41`).
- `SegmentChar::compose` is serial: length adds, upstream input slew/driven cap survive, downstream output slew/load cap survive, delay adds, and downstream source-boundary switching power is subtracted once (`src/operation/iCTS/source/database/characterization/SegmentChar.hh:60`, `src/operation/iCTS/source/database/characterization/SegmentChar.hh:77`).
- `HTreeTopologyChar::compose` is binary: levels add, upstream input slew/driven cap survive, downstream output slew/load cap survive, delay adds, and downstream raw power is doubled except for source-boundary switching power (`src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh:74`, `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh:94`).
- Segment joins require `upstream.output_slew_idx == downstream.input_slew_idx` and `upstream.load_cap_idx == downstream.driven_cap_idx` (`src/operation/iCTS/source/module/characterization/SegmentTraits.hh:31`, `src/operation/iCTS/source/module/characterization/SegmentTraits.hh:45`, `src/operation/iCTS/source/module/characterization/SegmentTraits.hh:52`).
- H-tree joins require `upstream.output_slew_idx == downstream.input_slew_idx` and `ceil(upstream.load_cap_idx / 2) == downstream.driven_cap_idx` (`src/operation/iCTS/source/module/characterization/HTreeTraits.hh:32`, `src/operation/iCTS/source/module/characterization/HTreeTraits.hh:43`, `src/operation/iCTS/source/module/characterization/HTreeTraits.hh:57`).
- State-frontier pruning groups by boundary buckets, leaf load cap, source-boundary switching, terminal semantic, monotonic boundary state, and source-exposed load count, then retains non-dominated delay/power entries (`src/operation/iCTS/source/module/characterization/Frontier.hh:99`, `src/operation/iCTS/source/module/characterization/Frontier.hh:187`, `src/operation/iCTS/source/module/characterization/Frontier.hh:242`, `src/operation/iCTS/source/module/characterization/Frontier.hh:262`).

Pattern metadata is not optional. `BufferingPattern` carries buffer positions, cell masters, terminal branch-buffer state, and monotonic boundary state (`src/operation/iCTS/source/database/characterization/BufferingPattern.hh:88`). `TopologyPatternLibrary::materialize` converts a selected topology pattern ID into one segment pattern ID per H-tree level (`src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentLibrary.hh:410`). `ApplySelectedPatternToLevelPlans` copies those segment IDs and buffer-cell metadata into the selected `LevelPlan`s (`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:179`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:186`, `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc:193`). Any analytical solver that does not produce materializable pattern IDs cannot feed the current embedding path.

Root-driver compensation and sink-load region legality are also hard constraints:

- `RootDriverCompensationOptions` requires valid cap/slew lattices for strict boundary closure (`src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.hh:71`).
- Strict root boundary closure checks that the physical root-driver source-boundary load bucket matches the raw H-tree driven-cap bucket, and the root output slew bucket matches the raw top input slew bucket when strict slew closure is enabled (`src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc:632`, `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc:646`, `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc:765`).
- Sink-load legality computes real clustered leaf loads and records a required leaf-load cap covering index from the maximum exact group capacitance (`src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc:310`, `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc:345`, `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc:347`).
- Global filtering rejects candidates whose selected `leaf_load_cap_idx` does not cover the required real sink-load cap index (`src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc:572`, `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc:597`).

### Analytical model variables

A practical analytical solver should use physical units internally and convert only at validation boundaries:

- `depth d`: selected H-tree depth, from existing `ResolveDepthCandidates`.
- `level i`: `0..d-1`, root to leaves.
- `p_i`: selected materializable segment pattern ID for level `i`; initially discrete shortlist, not continuous.
- `l_i`: aligned level length in microns or existing `aligned_length_idx`.
- `s_i`: input slew at the source side of level `i`, in ns.
- `s_{i+1}`: output slew at the downstream side of level `i`, in ns.
- `c_i`: load capacitance presented to the source side of level `i`, in pF.
- `b_i`: driven/source-boundary capacitance of level `i`, in pF.
- `leaf_cap_req`: required real leaf load cap in pF from `ResolveSinkLoadRegionLegality`.
- `D_i`, `P_i`: level delay and power from the fitted model for `(p_i, l_i, s_i, c_i)`.
- `D_root`, `P_root`: root-driver compensation, preferably still delegated to `RootDriverCompensationPass`.
- Optional `z_{i,p}`: binary pattern-selection variable if using a mixed-integer formulation. Avoid this initially unless a solver dependency is explicitly accepted.

Use `UniformValueLattice` only at boundaries: convert `s_i`, `c_i`, `b_i`, and leaf cap to covering indices before calling existing validation (`src/operation/iCTS/source/database/characterization/ValueLattice.hh:40`, `src/operation/iCTS/source/database/characterization/ValueLattice.hh:62`, `src/operation/iCTS/source/database/characterization/ValueLattice.hh:70`).

### Fitted model forms

For each materializable segment pattern `p` at aligned length `l`, fit these surfaces over the existing characterization samples:

```text
slew_out_p,l(s, c)  = a0 + a1*s + a2*c                         affine
delay_p,l(s, c)     = b0 + b1*s + b2*c + b3*s^2 + b4*s*c + b5*c^2
power_p,l(s, c)     = q0 + q1*s + q2*c + q3*s^2 + q4*s*c + q5*c^2
drive_cap_p,l(s, c) = r0 + r1*s + r2*c                         usually close to topology/pattern constant
```

Where possible, use affine `slew_out` and `drive_cap`; keep quadratic terms for delay/power. A quadratic `slew_out` equality makes the recursive timing constraints nonconvex if enforced exactly. If the fitted output slew is monotone and used as an epigraph inequality (`s_{i+1} >= slew_out(...)`), the optimum should be tight under delay/power minimization while remaining easier to solve.

Recommended fit hygiene:

- Normalize `s` by `max_slew_ns` and `c` by `max_cap_pf` before fitting. Store model coefficients in normalized units to reduce conditioning problems.
- Fit only within the bounding box covered by `CharBuilder` samples; reject extrapolation unless a fallback margin is explicitly configured.
- Enforce monotonicity by coefficient clipping or post-fit validation: output slew and delay should not decrease as input slew or load cap increases across the sampled domain.
- Record maximum absolute and relative residual per metric. Do not allow the analytical solver to replace native selection when residuals exceed a small bucket-aware threshold, e.g. more than half a slew/cap bucket for boundary metrics or a configured percentage for delay/power.
- Preserve source-boundary switching power if power composition still needs it; otherwise root/topology power comparisons can drift from `HTreeTopologyChar::compose`.

### Formulation option A: analytical ranking with discrete repair (recommended first)

This option keeps the discrete legal pattern set but uses fitted models to score and prune before native expensive joins explode.

Inputs:

- Existing `full_level_plans` and `depth_candidates`.
- Existing `BufferPatternLibrary` and base/synthesized `SegmentFrontierCatalog`.
- Fitted models for the segment entries in each required length frontier.
- Existing boundary constraints, fanout options, compensation options, and sink-load legality context.

Algorithm:

1. For each candidate depth, construct a small per-level shortlist from the segment frontier for that level length and terminal kind.
2. Rank candidate segment patterns by fitted delay/power and boundary slack at representative `(s, c)` points: top boundary, median lattice point, and leaf-load requirement.
3. Run a continuous recursion over each candidate sequence:
   ```text
   s_{i+1} >= slew_out_{p_i,l_i}(s_i, c_i)
   c_i >= 2 * b_{i+1}              for binary H-tree levels
   c_{d-1} >= leaf_cap_req          at the leaf side
   D = sum_i D_i(s_i,c_i) + D_root
   P = P_0 + 2*P_1 + 4*P_2 + ... + 2^(d-1)*P_{d-1} + P_root
   ```
4. Keep the best `K` sequences per depth by an objective such as `P + lambda*D` or an epsilon-constrained Pareto sweep.
5. Materialize each shortlisted sequence into the existing `TopologyPatternLibrary` shape or use the native `EvaluateCandidateBuild` on a narrowed frontier.
6. Validate with existing root compensation and sink-load-region coverage.
7. Select using the existing global Pareto/median policy or a documented analytical equivalent.

Objective choices:

- Closest to current behavior: build an analytical Pareto set by sweeping `D <= tau` and minimizing `P`; then apply the existing power-ordered median policy to the validated candidates.
- Simpler MVP: minimize `P_norm + lambda * D_norm`, with `lambda` emitted in logs and test fixtures.
- Constraint-oriented: minimize power subject to `D <= clock_period_budget` if a real timing target is introduced later.

Why this is safest:

- It still returns concrete `PatternId`s and `LevelPlan` metadata for embedding.
- It preserves the native branch-buffer, monotonic-boundary, fanout, root-driver, and sink-load checks.
- It can be hidden behind fallback. If the fit or optimizer fails, native search still works.
- It provides direct A/B validation against native selected delay/power, depth, pattern IDs, frontier counts, and runtime.

Expected integration boundary:

```text
HTree.cc
  after SegmentFrontierCatalog is available
  before SearchTopologyDepthCandidates
    -> analytical_solver::TrySolve(...)
       returns optional selected candidate or narrowed candidate pools
  if analytical success and validation success:
       fill BuildResult through existing selected-candidate path
  else:
       call native SearchTopologyDepthCandidates
```

### Formulation option B: fixed-sequence convex QCQP / small nonlinear solve

This option assumes a fixed sequence of segment patterns `p_i`; the optimizer only solves continuous boundary variables. With affine output-slew/drive-cap models and convex quadratic delay/power models, the solve is close to a convex QCQP if recursive constraints are inequalities and Hessians are positive semidefinite.

Variables:

```text
s_i in [slew_min, slew_max], i=0..d
c_i in [cap_min, cap_max], i=0..d-1
b_i in [cap_min, cap_max], i=0..d-1
D_i, P_i
```

Constraints:

```text
s_0 >= requested_top_input_slew       if min_top_input_slew_ns is present
s_{i+1} >= slew_out_{p_i,l_i}(s_i,c_i)
b_i >= drive_cap_{p_i,l_i}(s_i,c_i)
c_i >= 2*b_{i+1}                      for i < d-1
c_{d-1} >= leaf_cap_req
all fitted-model inputs inside sampled domain
```

Objective:

```text
minimize sum_i 2^i * P_i + alpha * sum_i D_i
```

or epsilon form:

```text
minimize sum_i 2^i * P_i
subject to sum_i D_i <= tau
```

Pros:

- Very fast for one fixed sequence.
- Useful as a ranker for option A.
- Can be implemented with a local active-set/grid-refinement routine because variable count is small.

Cons:

- Does not choose buffer positions/cell masters by itself.
- If fitted surfaces are nonconvex or equality recursion is required, a generic nonlinear solver would be needed.
- Exact current selection is Pareto/median, not a single scalar weighted objective.

### Formulation option C: mixed-integer quadratic model for pattern selection

This option models pattern selection directly with binary variables.

Variables:

```text
z_{i,p} in {0,1} for pattern p available at level i
sum_p z_{i,p} = 1
s_i, c_i, b_i continuous
```

Constraints and objectives mirror option B, with big-M or convex-hull activation of the per-pattern fitted surfaces.

Pros:

- Closer to replacing native enumeration.
- Can optimize across pattern choices rather than ranking pre-enumerated sequences.

Cons:

- Requires an MIQP/MINLP-capable dependency or a custom branch-and-bound layer.
- Big-M constraints are numerically fragile unless bounds are tight.
- Pattern materialization, monotonic state, terminal semantics, source-exposed fanout, and branch-buffer forcing still need discrete constraints. Encoding those cleanly duplicates much of `SegmentLibrary.hh` and `TopologyPruning.cc`.

Recommendation: do not start here.

### Formulation option D: dynamic programming with analytical value functions

This option keeps the H-tree dynamic-programming shape but replaces full lattice buckets with fitted value functions or compressed frontier states.

Approach:

- For each level and terminal semantic, maintain a small set of continuous states `(s_out, c_drive, leaf_cap, delay, power, pattern_id)`.
- Compose states using analytical transforms instead of exact hash-join bucket equality.
- Snap only final candidates back to `UniformValueLattice` for native validation.

Pros:

- Resembles the current code, so fallback/debugging is natural.
- Avoids external solver dependencies.
- Can preserve Pareto-front semantics.

Cons:

- Easy to accidentally recreate native search complexity.
- Needs careful state clustering to avoid nondeterministic candidate drift.
- Harder to prove monotonic legality than option A.

This is a good second-stage optimization after option A proves that fitted models rank candidates well.

### Numerical robustness notes

- Use normalized variables for all fits and solves.
- Add explicit domain guards: `0 < s <= char_max_slew_ns`, `0 < c <= char_max_cap_pf`, valid length index, and valid fitted-model sample count.
- Treat lattice bucket boundaries conservatively. When converting a continuous value back to a covering index, add a small positive margin before `coveringIndex` to avoid selecting a candidate that barely undercovers due to floating-point roundoff.
- Preserve native failure reasons where possible: `missing_required_segment_frontiers`, `empty_frontier_after_root_boundary_closure`, `no_sink_load_region_legal_frontier_entries`, `sink_load_region_boundary_load_coverage_violation`, and `no_strict_boundary_feasible_solution_any_depth`.
- Emit fit and solve diagnostics via schema/log helpers, not `std::cout`.
- Do not throw from solver failures. Return a result with `success=false`, a reason, residual stats, and `fallback_to_native=true`.
- Keep deterministic ordering for ties. Current selection has deterministic tie-breaks by driven cap, output slew, delay, power, load cap, input slew, and pattern ID (`src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc:103`).
- Do not compare raw double equality for fitted candidate selection. Current discrete code compares exact stored metrics from characterization; analytical output should use tolerances and stable sort keys.
- Beware unit drift: topology lengths are DBU-derived and converted with `dbu_per_um` before length alignment (`src/operation/iCTS/source/flow/synthesis/htree/plan/Plan.cc:84`); fit inputs should be ns/pF/um, not indices except at validation boundaries.

### Validation against native H-tree

Suggested validation phases:

1. Unit tests on synthetic `SegmentChar` grids:
   - Fit affine/quadratic models from known surfaces.
   - Confirm solver respects top slew, leaf cap, fanout, and branch-buffer constraints.
   - Confirm out-of-domain models fall back to native.
2. Existing degenerate tests:
   - Preserve missing-driver, empty-load, and single-load behavior from `HTreeTest` (`src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc:171`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc:232`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc:253`).
3. Real-tech smoke:
   - Reuse `SynthesizesMaterializedHTreeFromRealClockLoads` expectations: success, best char/pattern, selected depth, feasible frontier counts, level count, inserted pins/nets, load preservation, and report artifacts (`src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeTest.cc:104`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeTest.cc:106`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeTest.cc:110`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeTest.cc:121`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechSmokeTest.cc:126`).
4. Branch-buffer and top-boundary regression:
   - Preserve forced terminal branch-buffer materialization (`src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc:52`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc:91`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc:102`).
   - Preserve caller-facing top-boundary coverage and impossible-boundary fallback behavior (`src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc:157`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc:202`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc:253`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc:260`).
5. Matrix comparison:
   - Extend the ARM9 matrix record to compare native vs analytical selected depth, pattern ID, delay, power, root compensation validity, fallback, runtime, and fit residuals (`src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechMatrixSupport.cc:84`, `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechMatrixSupport.cc:194`).

Acceptance thresholds should be explicit:

- Analytical result must pass the same embedding and legality checks as native.
- Analytical selected delay/power should be within a configured tolerance of native or better under the same feasibility constraints.
- Runtime should improve on high-frontier cases, not just equal smoke cases.
- Any native fallback must be visible in report logs and should not change the final result semantics.

### Integration boundaries for `analytical_solver`

Proposed files if implemented later:

- `AnalyticalSolver.hh/.cc`: public submodule entry, result type, and fallback reason.
- `AnalyticalModel.hh/.cc`: model coefficient storage and evaluation.
- `AnalyticalFit.hh/.cc`: fit construction from `CharBuilder`/`SegmentChar` samples.
- `AnalyticalCandidate.hh/.cc`: candidate sequence and conversion to topology-pattern metadata.
- `AnalyticalValidation.hh/.cc`: conversion to lattice buckets and calls into existing root/sink legality checks.

Suggested API:

```cpp
namespace icts::htree {

struct AnalyticalSolveRequest
{
  const Tree* topology = nullptr;
  const std::vector<HTree::LevelPlan>* full_level_plans = nullptr;
  const std::vector<unsigned>* depth_candidates = nullptr;
  const SegmentFrontierCatalog* segment_frontier_catalog = nullptr;
  const BufferPatternLibrary* segment_pattern_library = nullptr;
  const CharBuilder* char_builder = nullptr;
  BoundaryConstraints boundary_constraints;
  RootDriverCompensationOptions root_driver_compensation_options;
  HTreeFanoutPruningOptions fanout_options;
};

struct AnalyticalSolveResult
{
  bool success = false;
  bool fallback_to_native = false;
  std::string failure_reason;
  CandidateBuildEvaluation selected_evaluation;
  CandidateCharRef selected_ref;
  std::vector<DepthSummary> depth_summaries;
  double max_slew_residual_ns = 0.0;
  double max_cap_residual_pf = 0.0;
  double max_delay_residual_ns = 0.0;
  double max_power_residual_w = 0.0;
};

auto TrySolveAnalyticalHTree(const AnalyticalSolveRequest& request) -> AnalyticalSolveResult;

}  // namespace icts::htree
```

Keep the first version `PRIVATE` to `icts_source_flow_synthesis_htree`; do not expose it through API. It should link existing htree submodules rather than duplicating include directories. If new code needs linear algebra, prefer a tiny local normal-equation/QR helper for 3- or 6-coefficient least-squares fits unless the project already exposes Eigen through a target. A large solver dependency should be a deliberate design decision, not incidental to this feature.

### External references

- No web references were required for this internal research pass.
- Local third-party scan found LEMON LP/MIP interfaces under `src/third_party/lemon/`, but no evidence that htree/iCTS currently wires a numerical optimization target into the htree flow.
- Local scan found Eigen references inside `src/third_party/spectra/test/`, but no evidence that htree/iCTS currently depends on an Eigen target for production code.
- Practical implication: the recommended MVP should avoid a new external solver dependency and use fitted ranking plus native discrete validation. If a future design chooses MIQP/QCQP, add a separate dependency review.

## Caveats / Not Found

- The Trellis current-task script reported no active task, even though the dispatch prompt provided `.trellis/tasks/05-14-analytic-htree-construction`; this research was written to the explicitly provided task directory.
- The task `prd.md` still contains placeholder requirements. This research infers the analytical-solver goal from the dispatch prompt and existing code shape.
- No existing `analytical_solver` code was found under `src/operation/iCTS/source/flow/synthesis/htree/`.
- The current native selection policy is not equivalent to a single scalar objective. It builds delay/power Pareto fronts and chooses a power-ordered median (`src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc:460`). Any analytical weighted objective will be an approximation unless it explicitly reconstructs a Pareto set.
- Fitted models can rank candidates cheaply, but they cannot by themselves produce materializable buffer positions and cell masters. The selected result still needs `BufferingPattern` and `TopologyPatternLibrary` metadata for embedding.
- Root-driver compensation depends on physical load estimation and direct STA adapter queries; it should remain delegated to `RootDriverCompensationPass` until there is a separate validated root-driver model.
- Sink-load legality depends on actual load groups, routing/electrical clustering, and max real load cap. It should remain delegated to `ResolveSinkLoadRegionLegality`.
- A full MIQP/MINLP formulation is possible on paper but duplicates much of the existing discrete legality machinery and would require a solver/dependency decision not present in the current codebase.
