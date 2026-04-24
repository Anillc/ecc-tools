# Research: fast_clustering algorithm design for iCTS topology

- Query: Research fast_clustering algorithm design for iCTS topology. Inspect existing linear_clustering implementation and shared clustering/electrical evaluators. Focus on input/output contract, design constraints to preserve, runtime hotspots in linear_clustering, feasible fast algorithm candidates that improve both runtime and score, risks/edge cases.
- Scope: internal
- Date: 2026-04-24

## Findings

### Files Found

- `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.hh`: public linear clustering facade and API shape to mirror.
- `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc`: default strategy exploration, execution orchestration, result materialization, logging.
- `src/operation/iCTS/source/module/topology/config/TopologyConfig.hh`: shared `LinearClusteringConfig` knobs and constraint/scoring enums.
- `src/operation/iCTS/source/module/topology/clustering/Clustering.hh`: shared `ClusterResult`, `ClusterElectricalSummary`, `ClusterElectricalEvaluation`, and `Clustering` facade.
- `src/operation/iCTS/source/module/topology/clustering/Clustering.cc`: bridge from shared facade into linear clustering and `ConstraintEvaluator`.
- `src/operation/iCTS/source/module/topology/linear_clustering/LinearClusteringTypes.hh`: internal segment, score, constraint, and electrical metric types used by linear clustering.
- `src/operation/iCTS/source/module/topology/linear_clustering/LinearOrderGenerator.cc`: Hilbert/density projection and sorting logic.
- `src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc`: rotation sweep, greedy segment splitting, per-rotation score cache.
- `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc`: fanout/diameter/cap/routing legality evaluation and exact electrical construction.
- `src/operation/iCTS/source/module/topology/linear_clustering/ClusteringEvaluator.cc`: score policy for legal segments.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc`: current default CTS sink clustering consumer.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc`: shared electrical evaluator usage for actual-load legality.
- `src/operation/iCTS/test/common/linear_clustering/metrics/ClusterGeometrySupport.*`: shared-ish test helpers for cluster geometry metrics.
- `src/operation/iCTS/test/module/topology/linear_clustering/realtech/support/*`: reusable real-tech linear clustering strategy, legality, and artifact helpers.
- `src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc`: existing retained linear strategy benchmark/ranking pattern.
- `src/operation/iCTS/source/module/routing/Router.hh` and `.cc`: routing facade used by exact electrical evaluation.

### Input / Output Contract

- Mirror `LinearClustering`'s public call shape: `buildElectricalBaseConfig(std::size_t max_fanout, double max_cap)`, `runDefault(const std::vector<Pin*>&, const LinearClusteringConfig&)`, and `run(const std::vector<Pin*>&, const LinearClusteringConfig&)` (`src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.hh:41`).
- `buildElectricalBaseConfig` currently sets only `max_fanout`, `max_diameter = std::numeric_limits<int>::max()`, and `max_cap`, leaving all other defaults from `LinearClusteringConfig` (`src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:356`).
- The shared output is `ClusterResult`, with `clusters`, `centers`, and `electrical_summaries` vectors (`src/operation/iCTS/source/module/topology/clustering/Clustering.hh:66`). Fast clustering should keep `centers.size() == clusters.size()` and `electrical_summaries.size() == clusters.size()` for any electrically evaluated result.
- Linear materialization filters null pins, skips empty segments, computes each cluster center as rounded geometric center, and preserves segment order in output (`src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:185`).
- Empty input returns an empty `ClusterResult` without error in both `runDefault` and `run` (`src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:365`, `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:411`).
- `LinearOrderGenerator` filters null pins before ordering and stores original input index for deterministic ties (`src/operation/iCTS/source/module/topology/linear_clustering/LinearOrderGenerator.cc:504`).
- CTS synthesis consumes clusters by index, skips empty clusters, places cluster buffers at `ClusterResult.centers`, and fails if no valid clustered buffers are produced (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:456`). This means fast clustering must not return a nominally successful result with only empty clusters.
- Current default CTS flow calls `TopologyGen::defaultLinearClustering(sinks, clustering_config)` when sink clustering is enabled (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:548`). The PRD says not to switch this flow in this task; fast facade additions should be benchmark-only/future integration surfaces.

### Design Constraints To Preserve

- `LinearClusteringConfig` owns fanout, diameter, capacitance, ordering, split, routing, root, scoring, sweep, exact-cap, density, routing-layer, and wire-width knobs (`src/operation/iCTS/source/module/topology/config/TopologyConfig.hh:101`).
- Constraint semantics:
  - `max_fanout > 0` enforces cluster size, while `max_fanout == 0` means no explicit fanout limit in `ConstraintEvaluator` (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:260`).
  - `max_diameter > 0` enforces existing diameter, while `max_diameter <= 0` disables diameter checking (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:265`).
  - The existing diameter definition is Manhattan span of the bounding box, `(max_x - min_x) + (max_y - min_y)`, not an arbitrary pairwise scan (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:326`). Fast clustering must use this definition for legality and score comparison.
  - Finite `max_cap` uses pin-cap lower bound before exact routing and rejects immediately when lower bound exceeds `max_cap` (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:245`, `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:270`).
  - Exact electrical evaluation, when active, may reject routing failure or routed total capacitance over `max_cap` (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:275`).
- Root/routing semantics:
  - Exact synthetic root uses `LinearRootPolicy::kCenter` or median (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:59`).
  - Exact routing root is legalized away from overlapping load points before routing (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:119`).
  - Router kind dispatch supports FLUTE, SALT, BST, and CBS (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:418`).
  - Routing layer and wire width fall back to global `CONFIG_INST` values when unset in the clustering config (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:64`, `src/operation/iCTS/source/module/routing/Router.cc:55`).
- Scoring semantics:
  - `kMaxDiameter` scores by cluster diameter; singleton/zero-diameter clusters are penalized by `config.max_diameter` when positive so singletons do not become free (`src/operation/iCTS/source/module/topology/linear_clustering/ClusteringEvaluator.cc:35`).
  - `kTotalWirelength` scores exact routed wirelength when available, otherwise diameter proxy, multiplied by `wirelength_weight` (`src/operation/iCTS/source/module/topology/linear_clustering/ClusteringEvaluator.cc:46`).
  - The linear default chooses the legal candidate with the smallest `partition.total_score`, tie-broken by strategy label (`src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:385`).
- Exact-cap caveat to preserve intentionally: `SequenceSplitter` passes `config.enable_exact_cap` into candidate evaluation (`src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:387`, `src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:440`). By contrast, the public helper `Clustering::evaluateClusterElectrical(loads, anchor, config)` forces exact need when `enable_exact_cap`, `always_build_exact_cap`, or finite `max_cap` is present (`src/operation/iCTS/source/module/topology/clustering/Clustering.cc:215`). Fast `run` should match linear's clustering semantics unless the task explicitly chooses stricter final validation for benchmark legality.
- Output legality should cover every non-null input load exactly once. Existing test artifact code treats missing load-to-cluster assignment as an error (`src/operation/iCTS/test/module/topology/linear_clustering/realtech/support/cluster/LinearClusteringRealTechCluster.cc:299`).
- New source must follow iCTS backend constraints: `.hh`/`.cc`, PascalCase file names, required headers, no exceptions, LOG macros, schema/report helpers for `cts.log`, and CMake updates before implementation (`.trellis/spec/project-constraints.md:18`, `.trellis/spec/project-constraints.md:60`).
- A new topology submodule should follow the existing `add_subdirectory`/library target pattern in `source/module/topology` (`src/operation/iCTS/source/module/topology/CMakeLists.txt:1`, `src/operation/iCTS/source/module/topology/linear_clustering/CMakeLists.txt:1`) and backend directory/CMake rules (`.trellis/spec/backend/directory-structure.md:66`).

### Runtime Hotspots In `linear_clustering`

- `runDefault` executes four retained strategies and picks the best (`src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:274`, `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:372`). This multiplies ordering, splitting, routing, and scoring work by four.
- Each retained default strategy uses `kPrefixAndStridedSweep` with default retained `strided_sweep_count = 4` and fanout-derived prefix count (`src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:248`, `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc:281`). With `max_fanout = 32`, one strategy can evaluate up to roughly `32 * 4 = 128` cyclic offsets; four retained strategies can approach 512 offsets before candidate-window retries.
- Combined sweep expands each strided anchor into a prefix-length sequential window and de-duplicates offsets (`src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:79`, `src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:167`). This is a quality-oriented sweep, not cheap exploration.
- Every rotation builds a copied rotated load vector (`src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:63`, `src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:242`, `src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:270`, `src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:298`), creating O(offset_count * load_count) memory traffic.
- `SegmentScoreCache` is scoped per rotated load vector, so exact segment evaluations are not reused across offsets or across retained strategies (`src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:244`, `src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:272`, `src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:300`).
- Forward/reverse candidate search scans from the largest legal fanout window toward singleton until it finds a legal segment (`src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:383`, `src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:409`). Tight cap or diameter can trigger many segment evaluations per emitted cluster.
- Bidirectional greedy evaluates front and back candidates at every iteration, then chooses by score/size/tie (`src/operation/iCTS/source/module/topology/linear_clustering/SequenceSplitter.cc:460`). This doubles many candidate searches relative to one-direction greedy.
- Segment evaluation scans the candidate segment to compute bbox span, median root, and center root (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:295`). Fanout bounds cap this scan, but it happens many times under many offsets.
- Exact electrical evaluation is the dominant hotspot under finite `max_cap` and `enable_exact_cap=true`: it queries pin caps, legalizes a root, builds clock terminals, routes via FLUTE/SALT/BST/CBS, validates the tree, builds an RC tree, queries wire RC, and runs timing update (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:349`, `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:361`, `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:386`, `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:418`, `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:438`, `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:439`).
- Discrete Hilbert sorting is O(n log n) and computes indexes/tangent tie-breakers (`src/operation/iCTS/source/module/topology/linear_clustering/LinearOrderGenerator.cc:578`), but for default real-tech cap-limited runs it is likely cheaper than repeated exact routing.
- Existing real-tech strategy sweep code also evaluates many strategy combinations and chooses by `selection_score` (`src/operation/iCTS/test/module/topology/linear_clustering/realtech/support/LinearClusteringRealTechStrategy.cc:107`, `src/operation/iCTS/test/module/topology/linear_clustering/realtech/support/LinearClusteringRealTechStrategy.cc:408`). The production `runDefault` retained that exploration in reduced top-4 form.

### Feasible Fast Algorithm Candidates

#### Recommended Candidate: Spatial Grid / Cell-Order Seeding + Local Nearest Packing + Exact Repair

Use a two-level global-to-local algorithm:

1. Build active load entries `(Pin*, Point<int>, original_index)` from non-null input. Preserve original index for deterministic ties.
2. Build a deterministic spatial index:
   - Compute bbox once.
   - Choose grid resolution from `target_size = max_fanout > 0 ? max_fanout : 32`.
   - Use about `ceil(load_count / target_size)` cells total, distributed by bbox aspect ratio.
   - If `max_diameter` is finite and positive, cap cell span so neighboring-cell expansion does not hide obvious diameter violations.
   - Order cells by discrete Morton/Hilbert key and then `(cell_x, cell_y)` to avoid unordered-map nondeterminism.
3. Iterate seed cells in global cell order. For each unassigned seed, expand neighboring cells in rings and gather local candidates sorted by distance to current anchor plus original index.
4. Pack a cluster incrementally using cheap lower bounds:
   - Update fanout, bbox diameter, and center/median candidate incrementally.
   - Reject additions that violate fanout or bbox diameter.
   - If a reusable pin-cap cache is exposed, reject additions whose pin-cap lower bound exceeds `max_cap`; otherwise defer cap to final exact repair.
   - Add local candidates while the resulting score density is acceptable. Avoid isolated singleton output when a legal merge exists because linear scoring penalizes singleton/zero-diameter clusters under `kMaxDiameter`.
5. Run local repair:
   - Merge undersized/singleton clusters into nearest neighboring clusters when both legality and score improve.
   - Split any cluster whose exact final evaluation fails cap or routing, using longest-axis median split or farthest-from-root removal.
   - Retry exact evaluation after split/merge, bounded by a small attempt count.
6. Finalize:
   - Compute `centers` as rounded cluster geometric centers to match linear output.
   - Build `electrical_summaries` from the same electrical evaluator used for legality.
   - If no legal complete partition can be produced but linear can, fall back to `LinearClustering::run` or return empty with an explicit warning depending on benchmark policy. For acceptance, fallback is safer than returning illegal clusters.

Why this can improve runtime:

- It reduces ordering from four full Hilbert strategies to one spatial-index pass.
- It reduces exact routing from candidate-window exploration to final clusters plus a bounded number of repair candidates.
- It avoids O(offset_count * load_count) rotated-vector copies.
- Expected complexity is about `O(n log n + n * local_candidate_count + cluster_count * exact_route(max_fanout))`, instead of `O(strategy_count * offset_count * n + many_exact_candidate_routes)`.

Why this can improve score:

- Linear clustering is restricted to contiguous windows in one-dimensional Hilbert order. Local nearest packing can form compact 2D neighborhoods across Hilbert discontinuities.
- Local merge/split repair can explicitly minimize the same final score instead of accepting first largest legal greedy segment.
- Under `kTotalWirelength`, exact final routes give a direct route-length score for local swaps/merges; under `kMaxDiameter`, bbox diameter is cheap and can guide cluster compactness.

Key implementation detail:

- Do not call `Clustering::evaluateClusterElectrical` for every candidate addition; it constructs a fresh `ConstraintEvaluator` and loses pin-cap cache (`src/operation/iCTS/source/module/topology/clustering/Clustering.cc:222`). Use one reusable evaluator instance for final cluster checks, or extract current `ConstraintEvaluator` into a shared clustering/electrical helper target so fast and linear share cache and exact semantics without depending on the linear module.

#### Candidate 2: Recursive Spatial Split + Adjacent Merge

Algorithm:

- Recursively split by longest bbox axis until each leaf passes fanout/diameter/pin-cap lower bounds.
- Sort each split deterministically by coordinate and original index.
- Merge adjacent leaves greedily when a merge remains legal and improves the shared final score.
- Run exact final validation and repair failed leaves with the same split policy.

Pros:

- Very robust for strict diameter constraints and highly nonuniform placement.
- Easy to reason about legality because each split monotonically reduces fanout and bbox span.
- Naturally handles sparse outliers better than a fixed grid.

Cons:

- Initial recursive splits can overproduce small clusters, which hurts score unless adjacent merge is strong.
- Without a local nearest pass, split boundaries can still be axis-artifact sensitive.

Best use:

- Fallback path when grid packing cannot repair a cap/routing failure or when bbox is extremely skewed.

#### Candidate 3: One-Order Sliding-Window Dynamic Programming With Deferred Exact Routing

Algorithm:

- Generate one deterministic spatial order, likely discrete Hilbert/Morton with the retained best transform.
- Use prefix structures for bbox, count, and optional pin-cap lower bounds to evaluate windows cheaply.
- Compute a dynamic-programming or shortest-path partition over legal lower-bound windows.
- Exact-route only selected windows and locally repair failed windows.

Pros:

- Lowest integration risk because it stays close to linear's order/segment model.
- Can improve score over greedy splitting by optimizing partition cuts globally for the selected order.
- Can dramatically reduce exact routing if exact work is deferred to selected windows.

Cons:

- Still inherits one-dimensional locality failures, so score may not beat the four-strategy linear default on all real designs.
- Prefix bbox for arbitrary windows needs careful min/max data structures; naive scans erase runtime gains.

Best use:

- Conservative first implementation if a full 2D packing module is too risky; pair it with local 2D merge/swap repair to improve score odds.

#### Candidate 4: Local Neighbor Graph Agglomeration

Algorithm:

- Build k-nearest candidate edges using grid buckets.
- Start with singleton clusters and greedily merge adjacent clusters by best score delta while legal.
- Recompute stale edges lazily and stop when no legal improving merge remains.

Pros:

- Strong score potential because it optimizes local merges directly.
- Naturally handles irregular clusters.

Cons:

- Priority-queue bookkeeping and stale edges add complexity.
- If exact electrical checks enter the merge loop, runtime can explode; use lower bounds during merge and exact only at final/polish stages.

Best use:

- Optional polishing pass after grid/recursive clustering, not the core MVP.

### Recommended MVP Design

- Implement `FastClustering` as a separate facade under `src/operation/iCTS/source/module/topology/fast_clustering` with the same call shape as `LinearClustering`.
- Share `LinearClusteringConfig` and `ClusterResult`; do not introduce a parallel config unless benchmark data proves a need.
- Implement core algorithm as "grid seed + nearest local packing + exact final validation + bounded split/merge repair."
- Add a small internal result scorer that matches `ClusteringEvaluator` final-score semantics for arbitrary clusters:
  - For `kMaxDiameter`, use bbox diameter with the singleton/zero-diameter penalty rule.
  - For `kTotalWirelength`, prefer exact summary wirelength, fallback to diameter.
- Prefer extracting/reusing electrical legality rather than duplicating it:
  - Current `ConstraintEvaluator` is physically in the `linear_clustering` target, but it is not conceptually linear-only.
  - Best structural option is to move or wrap it in a shared topology clustering/electrical target, then let both linear and fast depend on that target.
  - If avoiding restructuring in MVP, fast can use `Clustering::evaluateClusterElectrical` for final validation only, but this loses per-run pin-cap cache and may make final validation slower.
- For benchmark fairness, compute fast and linear quality using the same final `ClusterResult` scoring helper, not linear's internal `PartitionScore` for one side and an unrelated metric for the other.

### Benchmark/Comparison Notes

- Existing real-tech linear strategy ranking already writes `cts.log`, CSVs, aggregate ranking, case ranking, and order diagnostics (`src/operation/iCTS/test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc:1813`). Reuse this artifact structure for the new 20-case benchmark rather than inventing a new reporting format.
- Existing cluster metrics include cluster count, singleton count, min/max/avg cluster size, and max diameter (`src/operation/iCTS/test/common/linear_clustering/metrics/ClusterGeometrySupport.hh:38`). Extend with total score, exact cap violations, route failures, runtime, and per-case legality.
- Current linear real-tech experiment is Arm9 and representative synthetic focused, not the requested 20 placement-stage ICS55 design benchmark. A new discovery layer is needed for `/nfs/share/home/huangzhipeng/code-new/ecc-benchmark/runs/20260422_125008`.
- The benchmark acceptance should treat legality as a hard gate. Runtime and quality should be separate aggregate metrics because the PRD requires fast to improve both, not a combined score hiding one regression.

### Risks / Edge Cases

- Exact cap is the largest semantic risk. Lower-bound packing can create clusters whose exact routed cap exceeds `max_cap`; fast must split/repair after final exact evaluation, not merely report the violation.
- `enable_exact_cap=false` with finite `max_cap` is subtle: linear splitter checks pin-cap lower bound but does not necessarily route exact, while `Clustering::evaluateClusterElectrical` may force exact. Fast must intentionally choose whether to mimic `run` or use stricter final benchmark validation.
- Root collision matters for same-location or median-at-sink clusters. Exact evaluation legalizes root and can fail if legalization still overlaps a load (`src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc:119`).
- All-null input should return empty, matching `LinearOrderGenerator`'s null filtering.
- Single-pin and identical-coordinate clusters have diameter 0; scoring must apply the existing singleton/zero-diameter penalty under `kMaxDiameter`.
- `max_fanout == 0` and `max_diameter <= 0` disable those constraints in current evaluator semantics. Fast should not accidentally clamp them to default limits.
- Very sparse outliers can hurt both local nearest packing and recursive splitting. Use nearest merge/split repair and consider linear fallback for unrepaired cases.
- Determinism can be lost if grid cells are stored and iterated via unordered containers. Sort cell keys and candidate lists by stable keys.
- Large DBU coordinates should avoid overflow in center and sum computations. Existing test helper uses `long long` for center sums (`src/operation/iCTS/test/common/linear_clustering/metrics/ClusterGeometrySupport.cc:72`); fast should do the same.
- Output `centers` are buffer placement centers, while exact electrical roots may be median/center depending on config. Do not replace `ClusterResult.centers` with legalized/routed roots unless the CTS consumer contract changes.
- Depending directly from `fast_clustering` on `linear_clustering/ConstraintEvaluator` would undermine the "independent module" goal and create awkward target direction. Extracting shared evaluator code is cleaner but increases implementation scope.
- The existing `biPartition` topology code does not populate `electrical_summaries` (`src/operation/iCTS/source/module/topology/clustering/Clustering.cc:123`); fast must not copy that omission because this task compares electrical legality and score.

## External References

- None used. This research is based on internal iCTS code and Trellis specs only.

## Related Specs

- `.trellis/spec/project-constraints.md`: mandatory iCTS file naming, headers, logging, no exceptions, CMake-before-implementation, and validation requirements.
- `.trellis/spec/backend/directory-structure.md`: source/test placement and topology submodule CMake target rules.
- `.trellis/spec/backend/quality-guidelines.md`: naming, include, dependency visibility, and validation rules.
- `.trellis/spec/guides/code-reuse-thinking-guide.md`: relevant because fast clustering risks duplicating evaluator, geometry, and CMake wiring.
- `.trellis/spec/guides/cross-layer-thinking-guide.md`: relevant because exact electrical validation crosses Config, Wrapper, STAAdapter, routing, timing, and topology boundaries.

## Caveats / Not Found

- No `fast_clustering` source module exists yet under `src/operation/iCTS/source/module/topology`.
- No public shared arbitrary-cluster scorer was found. Existing scoring is segment-oriented inside `linear_clustering/ClusteringEvaluator`.
- No public batch electrical evaluator was found. `Clustering::evaluateClusterElectrical` exists but creates a new `ConstraintEvaluator` per call, which limits cache reuse for fast final validation.
- Existing real-tech linear benchmark infrastructure is useful, but it does not implement the requested 20 ICS55 placement-stage benchmark case discovery.
