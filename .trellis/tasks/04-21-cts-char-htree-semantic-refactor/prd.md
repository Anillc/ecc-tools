# Systematic char and h-tree semantic refactor

## Goal

Systematically redesign the iCTS characterization and H-tree composition semantics so the pipeline has one coherent contract for discretized length/electrical state, reusable composition, frontier pruning, and power accounting. This task is a semantic refactor, not a local patch series.

The mainline execution strategy for this task is refactor-first cleanup. A concrete refactor inventory is tracked in [refactor.md](./refactor.md) and should be treated as the implementation index for stale semantics, legacy naming, and compatibility debt discovered during this task.

## Mainline Shift

The current mainline for this task is now runtime diagnosis under auto-derived wire length unit, not further semantic cleanup.

The immediate objective is:

* locate why auto-derived `wire_length_unit_um` causes severe runtime inflation
* gather direct evidence with temporary instrumentation rather than relying on intuition
* validate or falsify the current working hypothesis that frontier growth during compose is exploding and dominating runtime
* build at least one smaller, more controllable test case that can reproduce the bottleneck faster than the full ARM9 end-to-end flow

The semantic refactor remains the prerequisite context, but the active delivery target is now bottleneck localization.

## What I already know

* The current implementation mixes multiple float-to-index policies for discretized quantities, so `length`, `slew`, and `load` do not obey one global lattice semantics.
* The current segment synthesis caches per-target-length results, but it does not solve the global problem "given several required lengths, construct the shortest reusable composition closure across them".
* Characterization power currently counts the auxiliary source and sink instances used as fixtures, so the stored power is not limited to the intended buffering pattern contribution.
* The current H-tree/segment pipeline is not frontier-only; raw composed entries are still retained and reused in later composition stages.
* `group`, `frontier`, and `compose` already have partial implementations, but the semantics are not fully unified across segment and H-tree, especially around monotonic boundary representation, relaxed composition, and downstream regrouping.
* Under fixed real-tech test support, `wire_length_unit_um` had been pinned to `25um`; that was not the source-level default behavior.
* Source-level default config leaves `wire_length_unit_um=0`, so production code falls back to auto-derived wire-length semantics unless runtime config overrides it.
* The first observed auto-derived full-sink clustered ARM9 ClockSynthesis smoke completed successfully but took about `1205s` wall time, versus about `70s` under the previous fixed-`25um` real-tech smoke setup.
* A dedicated smaller debug experiment on a sampled `192`-sink ARM9 clock reproduced the runtime gap: fixed-unit H-tree build took about `47.9s`, while auto-derived unit took about `417.1s`.
* The sampled debug experiment does **not** support the current hypothesis that H-tree compose/frontier explosion is the dominant bottleneck:
  * H-tree compose total input product only grew by about `1.82x`
  * max final frontier across explored depths did **not** grow; it was slightly smaller under auto-derived unit
  * measured H-tree compose time remained tiny in both cases (`~0.01s -> ~0.02s`)
  * segment required-length compose was not even exercised in that reproduction (`segment_compose_calls=0`)
* The same debug experiment shows the runtime is currently dominated by `CharBuilder` sweep growth under the finer auto-derived length unit:
  * `CharBuilder` elapsed time grew from about `46.6s` to `416.9s`
  * generated `segment_chars` grew from `13720` to `100200`
  * generated `patterns` grew from `831` to `6399`
  * executed STA samples in the sweep grew from about `83040` to `639830`

## New Problem Statement

Auto-derived wire length unit appears to trigger a major runtime regression, severe enough that the clustered ARM9 synthesis smoke moved from an approximately minute-scale run to a roughly twenty-minute-scale run while still producing a valid result.

The leading debugging hypothesis is:

* the smaller effective length unit increases the number of length bins
* finer bins increase intermediate frontier cardinality during segment/H-tree compose
* frontier sizes may grow explosively before pruning catches up
* compose-time frontier explosion, not routing/materialization, is likely the dominant runtime sink

This hypothesis must be tested with explicit instrumentation and a smaller reproduction case.

## Runtime Diagnosis Evidence

The current concrete evidence comes from the dedicated sampled-clock debug scenario `htree_builder_auto_unit_frontier_debug`, which compares:

* fixed runtime-configured `wire_length_unit_um = 25um`
* source-default auto-derived wire-length unit, which HTreeBuilder refines from topology lengths

Observed results on the same sampled `192`-sink ARM9 clock:

* wall/runtime moved from `47.9s` to `417.1s` (`8.71x`)
* `CharBuilder` effective length bins moved from `5` to `7`
* H-tree compose total input product moved from `10.09M` to `18.38M` (`1.82x`)
* max final frontier across explored depths moved from `2940` to `2720` (`0.93x`)
* H-tree compose measured time stayed negligible (`0.0096s` vs `0.0201s`)

Interpretation:

* the expensive stage in the sampled reproduction is not H-tree compose
* the dominant runtime increase is upstream characterization growth caused by the finer auto-derived length lattice
* extra length bins enlarge topology-slot count and buffering-pattern search space inside `CharBuilder`, which then multiplies STA sampling volume before H-tree composition even starts

## Implemented Runtime Fix

The implemented runtime fix is **not** an H-tree compose optimization. The fix targets the stage that the evidence identified as dominant: `CharBuilder` length sweep construction under auto-derived wire length.

Implemented changes:

* `CharBuilder::InitOptions` now supports explicit `wire_length_indices`, so H-tree flows can request a sparse/dense subset of length bins without changing the declared direct-characterization upper bound.
* `HTreeBuilder` now drives auto-derived characterization with an explicit reduced length-bin set instead of always sweeping every dense bin implied by the auto-derived unit.
* the current implementation derives:
  * a topology-driven `required_covering_iterations`
  * a runtime-configured hard cap `wire_length_iterations`
  * an effective direct-characterization limit equal to `min(required_covering_iterations, wire_length_iterations)`
* if topology-required lengths extend beyond the cap, direct characterization is limited to the dense prefix under the cap and longer required lengths are deferred to reusable segment compose
* if topology-required lengths stay within the cap, direct characterization may use only the actually required aligned bins
* temporary runtime instrumentation remains in `HTreeBuilder` so segment compose / H-tree compose size and timing can be inspected directly while validating the new policy

Rationale:

* the auto-derived runtime regression came from over-characterizing long segment lengths that were not necessary to build the actual H-tree levels
* direct characterization must obey the configured max-iter contract even under auto-derived wire-length unit
* longer topology-required lengths should be synthesized through reusable compose, because that is the purpose of the required-length closure logic
* when topology stays inside the cap, over-characterizing extra bins is unnecessary runtime cost

## Max-Iter Hard-Cap Semantic Fix

After the runtime diagnosis, the direct-characterization policy was tightened again to match the intended semantics of `wire_length_iterations`:

* if `wire_length_iterations = N`, direct characterization must never exceed length index `N`
* if the H-tree only needs shorter lengths, direct characterization may stop below `N`
* any required segment length whose aligned index is greater than `N` must be produced through reusable segment compose, not by direct characterization

Implemented enforcement:

* `HTreeBuilder` now records both:
  * `required_covering_iterations`: how far the auto-derived unit would need to go to cover all topology level lengths directly
  * `configured_wire_length_iterations`: the hard runtime-configured cap for direct characterization
* the effective direct-characterization upper bound is now `min(configured_wire_length_iterations, required_covering_iterations)`
* when required lengths extend beyond the cap, H-tree characterization uses a dense direct-characterized prefix `1..N` and lets segment compose build all longer required lengths
* `CharBuilder` now clamps any caller-provided `wire_length_indices` to `wire_length_iterations` and no longer back-solves a larger iteration count from `wire_length_indices.back()`

This closes the previous semantic leak where auto-derived mode could still report or effectively use a larger direct-characterization max than the configured `wire_length_iterations`.

## Post-Fix Results

### Small Reproducer (`192` sampled ARM9 sinks)

Final code result:

* fixed-unit runtime: `49.6s`
* auto-derived runtime: `295.4s`
* auto/fixed runtime ratio: `5.96x`
* auto-derived best result stayed aligned with the pre-fix safe path (`delay=0.168299ns`, `power=0.000065W`)
* H-tree compose still remained negligible; the sampled reproduction continues to show that compose is not the dominant runtime stage

### Full Clustered ARM9 Synthesis (real smoke)

Final code result on `ClockSynthesisRealTechSmokeTest.ClusteredModeBuildsCentroidBuffersAndUsesUnrestrictedHtreeFrontierAutoWireLengthUnit`:

* previous observed auto-derived wall/runtime: about `1205.1s`
* final code auto-derived wall/runtime: about `440.2s`
* speedup: about `2.74x`
* `CharBuilder` elapsed time dropped from about `1076.7s` to `419.3s`
* characterized `segment_chars` dropped from `232490` to `100770`
* characterized `patterns` dropped from `15375` to `6399`
* selected full-flow QoR stayed stable in the measured smoke:
  * `power = 539.617uW`
  * `delay = 0.3002ns`

Conclusion:

* the runtime issue is now materially mitigated on the real clustered ARM9 auto-unit smoke
* the fix preserves the measured best-solution metrics on that smoke while cutting the dominant `CharBuilder` stage substantially
* the remaining runtime is still overwhelmingly inside `CharBuilder`, but the pathological extra tail-bin characterization under auto-derived wire length is no longer the same magnitude bottleneck it was before

## Auto-Unit Matrix Results After Hard-Cap Fix

### ARM9 Full-Sink H-tree Matrix

Environment:

* test: `HTreeBuilderRealTechSmokeTest.Arm9FullSinkExperimentMatrixAutoWireLengthUnit`
* matrix: `iter={2,3,4,5}` x `step={10,15}`
* load count: `1221`
* measured auto-derived unit: `10.7445um`
* topology-level direct covering requirement under that unit: `required_covering_iterations = 10`

Observed per-point results:

* `iter=2, step=10`: runtime `5.50s`, final frontier `13736`, best `delay=0.711818ns`, `power=621uW`, direct char iter `2`
* `iter=2, step=15`: runtime `7.88s`, final frontier `21599`, best `delay=0.437115ns`, `power=423uW`, direct char iter `2`
* `iter=3, step=10`: runtime `5.43s`, final frontier `7100`, best `delay=0.513521ns`, `power=554uW`, direct char iter `3`
* `iter=3, step=15`: runtime `11.55s`, final frontier `17250`, best `delay=0.352515ns`, `power=386uW`, direct char iter `3`
* `iter=4, step=10`: runtime `15.13s`, final frontier `3080`, best `delay=0.434899ns`, `power=528uW`, direct char iter `4`
* `iter=4, step=15`: runtime `31.59s`, final frontier `15435`, best `delay=0.285305ns`, `power=366uW`, direct char iter `4`
* `iter=5, step=10`: runtime `46.45s`, final frontier `2828`, best `delay=0.426267ns`, `power=529uW`, direct char iter `5`
* `iter=5, step=15`: runtime `97.92s`, final frontier `14448`, best `delay=0.285143ns`, `power=365uW`, direct char iter `5`

Key validation points:

* all runs passed
* all runs stayed below the `600s` budget
* direct characterization stayed capped at the configured iter for every point, despite the auto-derived topology requiring `10` bins for full direct coverage
* runtime grew with the configured direct-characterization cap, but did not explode

### ARM9 Full-Sink ClockSynthesis Matrix (non-clustered, auto-unit)

Environment:

* test: `ClockSynthesisRealTechSmokeTest.Arm9FullSinkNonClusteredExperimentMatrixAutoWireLengthUnit`
* matrix: `iter={2,3,4,5}` x `step={10,15}`
* sink count: `1221`

Observed per-point results:

* `iter=2, step=10`: runtime `4.73s`, final frontier `3279`, best `delay=0.711818ns`, `power=621uW`, direct char iter `2`
* `iter=2, step=15`: runtime `7.01s`, final frontier `3134`, best `delay=0.437115ns`, `power=423uW`, direct char iter `2`
* `iter=3, step=10`: runtime `5.13s`, final frontier `580`, best `delay=0.513521ns`, `power=554uW`, direct char iter `3`
* `iter=3, step=15`: runtime `11.18s`, final frontier `3975`, best `delay=0.352515ns`, `power=386uW`, direct char iter `3`
* `iter=4, step=10`: runtime `14.94s`, final frontier `780`, best `delay=0.434899ns`, `power=528uW`, direct char iter `4`
* `iter=4, step=15`: runtime `31.79s`, final frontier `2100`, best `delay=0.285305ns`, `power=366uW`, direct char iter `4`
* `iter=5, step=10`: runtime `46.58s`, final frontier `534`, best `delay=0.426267ns`, `power=529uW`, direct char iter `5`
* `iter=5, step=15`: runtime `98.85s`, final frontier `2138`, best `delay=0.285143ns`, `power=365uW`, direct char iter `5`

Conclusion:

* the hard-cap semantic fix does not reintroduce the earlier runtime blow-up
* end-to-end runtime remains controlled under the requested matrix, with the worst observed point still below `100s`
* the direct-characterization cap is now externally visible in logs/results and matches the configured `iter` exactly at every matrix point

## Validated Problems

### 1. Global discretized semantics is inconsistent today

The current code does not use one global lattice semantics for `length`, `slew`, and `load`. Instead, multiple locations independently map physical values to indices with different rounding policies, and some later logic reasons backward from those derived indices. This matches the reported `float floor` / mixed discretization problem and should be corrected as a foundational semantic issue.

### 2. Required-length synthesis is not solving the reusable shortest-closure problem

The current segment synthesis computes best splits per target length and reuses cached results locally, but it does not optimize the whole required-length set as one reusable composition graph. This means "compose several required lengths with minimal total synthesized path inventory, and let later compose steps reuse the materialized results" is not the actual semantics today.

### 3. Characterization power includes source/sink fixture contribution

The current power path includes the auxiliary source and sink instances that are introduced to run characterization. As a result, stored power is not limited to the target buffering pattern itself. This matches the reported issue and should be fixed.

### 4. The composition pipeline is not frontier-only

The current segment and H-tree flow both keep raw composed entries alongside frontier entries and still consume those raw entries in later stages. Therefore "all composition is frontier-only" is not true in the current implementation. This should be changed if the intended invariant is frontier-only propagation.

### 5. `group` / `frontier` / `compose` semantics are only partially aligned

The current code already has a same-state frontier notion and non-dominated pruning by `delay/power`, but the full semantics are not unified:

* state grouping uses electrical boundaries plus a monotonic boundary abstraction, not an explicit canonical contract based on first/last buffer semantics
* relaxed composition weakens exact state matching into inequality-based feasibility
* downstream selection sometimes regroups by structural representative rather than the same frontier grouping rule

So the overall problem statement is valid, but `frontier` itself is closer to the desired definition than `group` and `compose` are.

## Assumptions (temporary)

* This refactor will preserve the overall iCTS module split, but internal data contracts and intermediate representations may change substantially.
* The first implementation scope is limited to the five validated semantic problems below. Previously identified extra high-risk issues are intentionally not included in the first scope unless they become unavoidable while refactoring.
* The refactor target is a single consistent semantics shared by characterization entries, segment composition, and H-tree composition.

## Open Questions

* None for the initial semantic contract. Additional implementation-level details can be resolved during refactor as long as they preserve the agreed `group/frontier/compose` contract.

## Requirements (evolving)

* Mainline development work is semantic refactor and cleanup, not compatibility patching.
* Use [refactor.md](./refactor.md) as the task-local inventory of wrong, stale, redundant, and no-longer-necessary code discovered under the frozen five-issue scope.
* Replace mixed float/discretize/back-solve behavior with one global lattice semantics for `length`, `slew`, and `load`.
* Redefine required-length synthesis so multiple required lengths are solved as a reusable shortest composition problem, not as isolated per-length reconstruction.
* Remove source/sink fixture cell power from characterization results.
* Restrict composition flow to frontier-only data propagation.
* Unify `group`, `frontier`, and `compose` semantics across segment and H-tree.
* Define `group` by external compose-visible contract, not by full internal structure.
* Add a mandatory development close-out experiment matrix on the ARM9 full-sink H-tree flow:
  - characterization iterations: `2 / 3 / 4 / 5`
  - slew/cap steps: `10 / 15`
  - execution mode: serial only
  - per-run runtime budget: each H-tree run must stay within `600s`
* Compare and report, for each experiment point:
  - runtime
  - final frontier composition count
  - best-solution outcome
* Analyze the experiment results and include the statistics in the final development report.
* Add temporary runtime instrumentation around frontier construction / compose so the system can report where intermediate set sizes and time explode under auto-derived wire length unit.
* Add a smaller targeted test to reproduce the auto-derived runtime issue more cheaply than the full ARM9 clustered synthesis smoke.
* Use the new test plus instrumentation to produce concrete evidence for or against the frontier-explosion hypothesis.

## Acceptance Criteria (evolving)

* [ ] There is one explicit discretized semantics for `length`, `slew`, and `load`, with no local reverse inference or ad hoc floating-point rounding in compose/build paths.
* [ ] Required-length synthesis produces a reusable minimal composition closure for the full required-length set.
* [ ] Characterization power excludes source/sink auxiliary fixture instances.
* [ ] Segment and H-tree composition propagate only per-group frontier entries.
* [ ] `group`, `frontier`, and `compose` have one shared semantic contract that applies consistently to segment and H-tree paths.
* [ ] ARM9 full-sink H-tree validation runs complete for the matrix `iter={2,3,4,5}` × `step={10,15}` in serial mode, with each H-tree runtime not exceeding `600s`.
* [ ] The final report includes runtime, final frontier composition count, and best-solution statistics for every validation point, plus analysis of the observed trends.
* [ ] There is instrumented evidence that identifies which stage dominates auto-derived runtime.
* [ ] There is at least one smaller reproduction test for the auto-derived runtime issue.
* [ ] The final report explains whether compose/frontier explosion is the actual bottleneck, with concrete counters/timings to support the conclusion.

## Definition of Done

* Refactor follows the agreed semantics rather than preserving incompatible legacy behavior behind fallback branches.
* Mainline stale/legacy code identified in [refactor.md](./refactor.md) is either removed, renamed to match current semantics, or explicitly retained only for justified compatibility reasons.
* Affected tests are updated to encode the new semantics.
* The final design is documented clearly enough that future composition changes do not reintroduce mixed semantics.
* The ARM9 full-sink validation matrix has been executed and its results have been analyzed before handoff.
* The auto-derived wire-length runtime regression has been narrowed to a concrete stage with evidence, not just hypothesis.

## Technical Approach

The refactor target is a frontier-first composition pipeline driven by explicit group semantics:

* `group` is an external composition contract, not structural identity
* internal pattern shape is not part of group identity if it does not change the compose-visible contract
* `frontier` is computed only within the same group and keeps same-group non-dominated `delay/power` solutions
* `compose(i, j)` must satisfy upstream/downstream electrical boundary compatibility and the monotonic boundary contract derived from the first/last exposed buffers
* pure-wire boundary semantics must be represented explicitly rather than overloaded through an ambiguous rank value

## Decision (ADR-lite)

**Context**: The previous code used electrical boundaries plus an implicit monotonic boundary state derived from first/last buffer ranks, but the intended semantics for `group` were still ambiguous.

**Decision**: `group` is defined by compose-visible boundary contract:

* same input/output electrical state
* same terminal semantic
* same first-boundary buffer presence
* same last-boundary buffer presence
* if a first boundary buffer exists, the same first-buffer monotonic size class
* if a last boundary buffer exists, the same last-buffer monotonic size class

The size notion here means monotonic size class / strength rank, not exact internal structure and not necessarily exact interior buffer sequence. Therefore patterns such as `x5->x4->x3` and `x5->x3->x3` belong to the same group when the other boundary conditions are equal.

**Consequences**:

* `group` can no longer rely on ambiguous `rank=0` semantics to represent both pure wire and unresolved rank
* the refactor should make boundary buffer presence explicit
* compose legality and frontier pruning can be defined consistently for both segment and H-tree paths

## Out of Scope

* The previously listed extra high-risk issues are not part of this first scoping pass.
* Small local patches that keep the current semantic inconsistencies alive.
* Unrelated CTS flow changes outside characterization and H-tree composition.

## Technical Notes

* Float/discretization semantic drift is visible across `CharBuilder.cc`, `HTreeBuilder.cc`, and `HTreeTraits.hh`.
* Per-length local synthesis behavior is in `HTreeBuilder.cc` segment synthesis logic.
* Power accounting includes fixture instances through `CharBuilder.cc` and `STAAdapter.cc`.
* Frontier-only is violated by current `all_raw_entries` / `current_raw_entries` retention in `HTreeBuilder.cc`.
* Unified group/frontier/compose semantics touch `Frontier.hh`, `BufferingPattern.hh`, `HashJoinEngine.hh`, `HTreeTraits.hh`, `SegmentChar.hh`, and `HTreeTopologyChar.hh`.
* The final validation needs an ARM9-based full-sink H-tree experiment harness that can record per-run runtime, final frontier composition count, and best-solution data for the matrix `iter={2,3,4,5}` × `step={10,15}`.
