# HTree Demand-Driven Frontier Refactor Analysis

## Scope

This note analyzes whether the current all-frontier fast path should become a more decoupled demand-driven frontier construction design instead of remaining a source-trunk-specific wrapper.

Relevant files inspected:

- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentLibrary.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunkSegment.cc`
- focused HTree tests under `src/operation/iCTS/test/flow/synthesis/htree`

## Current Coupling Points

`SegmentCandidateFrontierSet` is the central coupling point:

```c++
struct SegmentCandidateFrontierSet
{
  std::vector<SegmentChar> all_frontier_entries;
  std::vector<SegmentChar> branch_buffered_entries;
  std::vector<SegmentChar> leaf_unbuffered_entries;
};
```

The type stores three semantic frontiers directly and callers select vectors by field. This makes it easy to accidentally construct fields that a caller cannot consume.

Current public APIs:

- `SynthesizeSegmentEntrySets(...)`: builds all three semantic vectors.
- `SynthesizeSegmentAllFrontierEntrySets(...)`: builds only `all_frontier_entries`.

The second API is functionally correct, but it encodes a current source-trunk need as a separate public function rather than modeling consumer demand.

## Semantic Frontier Kinds

The code effectively has three semantic frontier kinds:

1. `all`: exact Pareto frontier over every segment candidate for a length.
2. `terminal_branch_buffered`: exact Pareto frontier over candidates whose segment pattern ends with a terminal branch buffer.
3. `terminal_leaf_unbuffered`: exact Pareto frontier over candidates whose segment pattern does not end with a terminal branch buffer.

`all` is not just another output kind. It is a dependency for composing every semantic kind:

- `all(target) = compose(all(left), all(right))`
- `branch(target) = compose(all(left), branch(right))`
- `leaf(target) = compose(all(left), leaf(right))`

This means every valid request should include or imply `all`.

## Consumer Analysis

`SourceTrunkSegment::build`:

- Calls `SynthesizeSegmentAllFrontierEntrySets(...)`.
- Checks only `entry_set.all_frontier_entries`.
- Filters only that vector by load cap, source drive cap, and optional min input slew.
- Never consumes `branch_buffered_entries` or `leaf_unbuffered_entries`.

Downstream HTree:

- `HTree::build` currently calls full `SynthesizeSegmentEntrySets(...)`.
- `TopologyPruning.cc` chooses `kBranchBuffered` only when `BoundaryConstraints::force_branch_buffer` is true.
- Otherwise it chooses `kAllFrontier`.
- `kLeafUnbuffered` exists in `SegmentEntrySelection`, but current production resolution does not return it.

Therefore the current production demand matrix is:

| Path | Uses all | Uses branch | Uses leaf |
| --- | --- | --- | --- |
| Source trunk segment | yes | no | no |
| HTree unrestricted | yes | no | no |
| HTree force branch buffer | yes | yes | no |

The old full HTree path builds leaf frontiers even though no production selector currently asks for them.

## Refactor Opportunity

The better abstraction is a request-based API:

- Request declares required length indices and semantic frontier kinds.
- Synthesis normalizes dependencies, especially `all`.
- Internal composition loops build only requested target kinds.
- Callers receive a catalog and ask for `(length_idx, kind)`.

This turns the current all-only fast path from a special-case public function into one request profile:

- source trunk: `all`
- unrestricted HTree: `all`
- branch-forced HTree: `all | branch`
- full diagnostic/equivalence: `all | branch | leaf`

## Recommended Implementation Shape

Add:

- `SegmentFrontierKind`
- `SegmentFrontierKindSet`
- `SegmentFrontierRequest`
- `SegmentFrontierCatalog`
- `SynthesizeSegmentFrontiers(...)`

Keep wrappers only if needed for a short migration window, and make them call the request-based API internally. The desired final public shape is one general API.

Avoid encoding source-trunk semantics in the API name. The request should carry semantics.

## HTree Readability Findings

`HTree::build` is readable at a coarse level but still carries too many responsibilities in one function. Low-risk extraction points:

- input validation and root-net initialization;
- topology generation and no-level handling;
- characterization run and boundary constraint resolution;
- segment frontier request construction and synthesis;
- depth search invocation;
- global coverage filtering and selection;
- selected sink-load legality and root-driver compensation;
- materialized pattern decoration into `LevelPlan`;
- embedding and summary emission.

`SegmentPruning.cc` has several helper names and comments that still reflect older filenames. This is a small cleanup candidate while touching the module.

`TopologyPruning.cc` mixes:

- seed construction;
- HTree frontier composition;
- depth-local filtering;
- global Pareto selection;
- sink-load-region coverage filtering.

This does not need a large split for the request API, but separating semantic frontier access from topology frontier composition would make the code easier to reason about.

`SourceTrunkSegment.cc` duplicates some delay/power Pareto selection concepts from topology pruning and some object-construction concepts from HTree embedding. These are not blockers for the frontier refactor, but they are candidates for a later cleanup if the current task remains contained.

## Risk Notes

Pattern ID stability:

- Skipping unused semantic frontiers can change internal segment pattern ID allocation.
- Source trunk already shows this kind of ID drift while preserving physical topology/QoR.
- For downstream HTree, the first slice should preserve full behavior or compare materialized patterns, not just numeric IDs.

Branch-buffer correctness:

- Branch-forced HTree must continue to select terminal branch-buffered segment patterns on every selected level.
- Existing branch-buffer regression tests are the right guardrail.

Leaf-unbuffered support:

- Leaf frontiers are not currently consumed in production, but the semantic kind should remain supported.
- Removing the type would narrow future design options and make equivalence/debug tooling worse.

## Suggested MVP

MVP should be an API and structure refactor:

1. Introduce request/catalog types.
2. Migrate source trunk and HTree consumers.
3. Preserve downstream HTree full request for the first commit.
4. Add tests proving request kind behavior.
5. Then, as a second commit or explicit sub-step, enable HTree demand narrowing with runtime/QoR validation.

This avoids mixing a mechanical decoupling refactor with behavior-affecting runtime changes.
