# CTS DRV-Aware Skew Post-Optimization Research

## Goal

Design a feasible post-processing skew optimizer for the current iCTS H-Tree flow. The optimizer should start from the existing buffered clock tree after long-wire buffering and level sizing, then iteratively reduce skew violations under a monotone-safe policy that only upsizes buffers and does not worsen global skew.

## What I already know

- The current public CTS flow is still `run_cts -> CTSFlowRunner::run`.
- The current net synthesis pipeline is `SinkClusteringOperator -> TopologyBuilderOperator -> LongWireBufferingOperator -> LevelSizingOperator`.
- The user wants a post-H-Tree skew optimizer that runs after the current H-Tree implementation, including long-wire breaking.
- The proposed direction is:
  - abstract current buffering structure
  - build a buffer size to cell delay relation table
  - maintain fast lookup for the worst skew sink pair
  - use LCA to find the common ancestor
  - only allow buffer upsizing
  - keep delay monotone and avoid global skew degradation
  - iterate until skew converges to the target bound
- The repo still contains an older `TreeBuilder::iterativeFixSkew(...)` path, but it is not part of the new default flow.

## Assumptions (temporary)

- This task is currently a research-and-design task, not immediate code implementation.
- The first target is backend-only integration inside `src/operation/iCTS/`.
- The preferred insertion point is after existing H-Tree construction, long-wire buffering, and level sizing, but before final evaluation/report.
- The optimization should preserve current external `run_cts` behavior unless a config switch is later added.

## Open Questions

- Should the optimizer be net-local only, or allowed to optimize against global cross-net worst skew metrics?
- Should the first implementation operate on estimated RC/timing only, or require incremental STA refresh at each iteration checkpoint?
- Is the optimization target strictly skew bound feasibility, or a multi-objective tradeoff with slew/cap/fanout/area?

## Requirements (evolving)

- Produce a repo-aligned technical proposal for a post-processing skew optimizer.
- Research related CTS/skew optimization literature and similar algorithmic patterns.
- Explain how to map the user's LCA + monotone upsizing idea onto current iCTS data structures and flow stages.
- Identify recommended ownership boundaries, likely files/modules, and integration points.
- Identify failure modes, convergence guards, and validation strategy.

## Acceptance Criteria (evolving)

- [ ] Current CTS flow and net-level operator chain are traced clearly.
- [ ] Existing repo logic relevant to skew fixing and timing propagation is summarized.
- [ ] External literature or comparable approaches are reviewed and connected to this problem.
- [ ] A concrete algorithm proposal is given with data model, iteration loop, stopping criteria, and complexity notes.
- [ ] The proposal identifies where to integrate into iCTS and what code modules are likely affected.

## Definition of Done

- Research summary is grounded in both repo inspection and external references.
- The proposed solution includes algorithm steps, data structure choices, and verification points.
- Scope boundaries and risks are stated explicitly.

## Out of Scope (explicit)

- Immediate code implementation.
- Full cross-layer wrapper/config exposure unless required by the final implementation phase.
- Replacing the existing H-Tree construction algorithm itself.

## Technical Notes

- Relevant repo entrypoints:
  - `src/operation/iCTS/source/module/flow/CTSFlow.cc`
  - `src/operation/iCTS/source/utils/synthesis_operator/NetSynthesisPipeline.cc`
  - `src/operation/iCTS/source/utils/synthesis_operator/LevelSizingOperator.cc`
  - `src/operation/iCTS/source/utils/tree_builder/TreeBuilder.cc`
- Relevant spec guides:
  - `.trellis/spec/backend/directory-structure.md`
  - `.trellis/spec/backend/error-handling.md`
  - `.trellis/spec/backend/logging-guidelines.md`
  - `.trellis/spec/backend/quality-guidelines.md`
  - `.trellis/spec/guides/code-reuse-thinking-guide.md`
  - `.trellis/spec/guides/cross-layer-thinking-guide.md`

## Repo Inspection Notes

- Current net synthesis order is topology build -> long-wire buffering -> topology summary logging -> level sizing.
- `LevelSizingOperator` already has:
  - delay-library enumeration
  - skew/slew/cap/length/fanout feasibility checks
  - net-topological reevaluation order through `state.net_records`
- The current solver state already tracks:
  - all generated solver nets
  - buffer instances by depth
  - parent/child tree links through `Node`
- Existing `TreeBuilder::iterativeFixSkew(...)` rebuilds timing through `BoundSkewTree`; it is not an upsizing-only post-processing path and is therefore a reference, not a direct reuse target.
- Buffer libraries are currently sorted from smaller to larger by area / init cap / delay intercept.
- Current timing propagation uses real library tables through `CtsCellLib::calcInsertDelay(slew_in, cap_out)`, not only a fixed linear proxy.

## Research Notes

### What similar work does

- Older CTS sizing work uses top-down slack or skew-budget propagation to convert global skew constraints into local sizing decisions.
- More recent work combines useful-skew scheduling with buffer sizing and sometimes layer assignment, usually via dynamic programming or mathematical programming.
- Robust formulations emphasize that sizing impact is operating-point dependent and must be checked against slew/cap/power or variation sensitivity.

### Constraints from this repo

- The topology is fixed after `TopologyBuilderOperator` and `LongWireBufferingOperator`.
- Timing can be reevaluated incrementally through the existing solver-net records.
- Per-depth global sizing already exists, but per-instance path repair does not.
- A new stage should live under `source/utils/synthesis_operator/` instead of `tree_builder/` or interface layers.

### Feasible approaches here

**Approach A: New post-level-sizing skew repair operator** (Recommended)

- How it works:
  - Run after `LevelSizingOperator`.
  - Keep topology fixed.
  - Iteratively identify the current worst sink pair, find their LCA, and search for beneficial upsizes on the fast branch.
  - Reuse existing timing propagation and feasibility checks for exact acceptance.
- Pros:
  - Matches the user's post-processing requirement.
  - Minimal impact on current topology / long-wire logic.
  - Easy to ablate and benchmark.
- Cons:
  - Needs a new local search loop and candidate cache.

**Approach B: Extend `LevelSizingOperator` into per-instance local search**

- How it works:
  - Fold skew repair into the current sizing operator and move from depth-uniform sizing to mixed local sizing.
- Pros:
  - One sizing phase only.
  - Can share more internal code.
- Cons:
  - Blurs ownership.
  - Harder to debug and benchmark separately.
  - Fights the current "level sizing" abstraction.

**Approach C: Reuse / adapt old `iterativeFixSkew`**

- How it works:
  - Route post-processing through `BoundSkewTree` refinement.
- Pros:
  - Existing code path exists.
- Cons:
  - Violates the desired "upsizing only / fixed topology" constraint.
  - Harder to make DRV-aware under the new pipeline ownership.

## Decision (working)

**Context**: We need a skew-focused post-processing stage that respects the current H-tree pipeline and the user's upsizing-only idea.

**Decision**: Prefer a new operator after level sizing, using exact timing reevaluation for acceptance and a fixed-topology local search around the current worst skew pair.

**Consequences**:

- We preserve the new pipeline structure.
- We avoid duplicating global flow logic in `tree_builder`.
- We must explicitly handle the fact that upsizing is not automatically equivalent to monotone delay increase under the current timing model.
