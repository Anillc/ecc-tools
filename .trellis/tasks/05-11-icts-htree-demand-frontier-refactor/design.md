# Technical Design

## Objective

Replace the binary `SynthesizeSegmentEntrySets(...)` versus `SynthesizeSegmentAllFrontierEntrySets(...)` split with a demand-driven frontier request/catalog design. The refactor should make semantic frontier needs explicit, let each consumer request only what it can use, and keep exact current CTS behavior unless a later validation step explicitly enables narrower default HTree requests.

## Current Type Taxonomy

Segment-level types:

- `SegmentChar`: characterized segment candidate with slew/cap/delay/power and a segment `PatternId`.
- `BufferingPattern`: materialized buffer pattern for one segment length, including buffer positions, cell masters, terminal branch-buffer flag, and monotonic boundary state.
- `PatternCompositionState`: the semantic state used by exact frontier composition and pruning. It contains `terminal_semantic` plus monotonic boundary state.
- `BufferPatternLibrary`: segment pattern metadata and composition-state cache. Composition creates new segment pattern IDs and inserts merged `BufferingPattern` objects.
- `SegmentCandidateFrontierSet`: current storage bundle with `all_frontier_entries`, `branch_buffered_entries`, and `leaf_unbuffered_entries`.

Topology-level types:

- `HTreeTopologyChar`: composed topology candidate over one or more segment levels.
- `TopologyPatternLibrary`: compact topology pattern DAG. Seed nodes reference segment pattern IDs; concat nodes reference upstream/downstream topology pattern IDs.
- `PatternSearchResult`: frontier plus topology library for one depth candidate.

Selection/result types:

- `CandidateBuildEvaluation`: per-depth build result, raw/filtered frontiers, best entry, boundary fallback details, and the depth-local topology pattern library.
- `CandidateCharRef`: global selection reference into per-depth candidate frontiers.
- `DepthSearchResult`: all depth evaluations plus global candidate pools and shared sink-load-region/root-driver compensation state.
- `HTree::BuildResult`: public synthesis result and object ownership. It currently mixes user-visible result fields, selected solution metadata, inserted object ownership, and tracing metadata.

## Consumer Matrix

| Consumer | Required semantic frontiers | Reason |
| --- | --- | --- |
| `SourceTrunkSegment::build` | `all` | It filters by load cap, driven cap, and optional input slew, then selects from all candidates. It never asks for terminal branch/leaf subsets. |
| Downstream HTree without `force_branch_buffer` | `all` | `ResolveSegmentEntrySelection` returns `kAllFrontier`, so topology seeds come from all candidates. |
| Downstream HTree with `force_branch_buffer` | `all | terminal_branch_buffered` | Selected seeds require branch-buffered candidates. Composition of branch frontiers still needs upstream all frontiers. |
| Current full/equivalence mode | `all | terminal_branch_buffered | terminal_leaf_unbuffered` | Preserves old full synthesis behavior for tests and diagnostics. |
| Future forced leaf-unbuffered mode | `all | terminal_leaf_unbuffered` | Not a production consumer today, but the semantic kind should remain representable. |

## Proposed API

Introduce a request and catalog boundary in `segment_pruning`:

```c++
enum class SegmentFrontierKind
{
  kAll,
  kTerminalBranchBuffered,
  kTerminalLeafUnbuffered,
};

class SegmentFrontierKindSet
{
 public:
  static auto AllOnly() -> SegmentFrontierKindSet;
  static auto BranchConstrained() -> SegmentFrontierKindSet;
  static auto Full() -> SegmentFrontierKindSet;
  auto contains(SegmentFrontierKind kind) const -> bool;
  auto add(SegmentFrontierKind kind) -> void;
};

struct SegmentFrontierRequest
{
  std::vector<unsigned> required_length_indices;
  SegmentFrontierKindSet required_kinds;
  bool preserve_full_pattern_id_sequence = false;
};

class SegmentFrontierCatalog
{
 public:
  auto find(unsigned length_idx, SegmentFrontierKind kind) const -> const std::vector<SegmentChar>*;
  auto require(unsigned length_idx, SegmentFrontierKind kind) const -> const std::vector<SegmentChar>&;
  auto has(unsigned length_idx, SegmentFrontierKind kind) const -> bool;
  auto countEntries(SegmentFrontierKindSet kinds) const -> std::size_t;
};

auto SynthesizeSegmentFrontiers(const std::vector<SegmentChar>& base_segment_chars,
                                BufferPatternLibrary& pattern_library,
                                const SegmentFrontierRequest& request) -> SegmentFrontierCatalog;
```

The exact names can change during implementation, but the design constraint is that callers select semantic kinds through a typed request and consume through typed accessors. Callers should not read storage vectors by field name.

## Composition Rules

For a target length synthesized from `left_length + right_length`:

| Requested target kind | Required upstream input | Required downstream input |
| --- | --- | --- |
| `all` | `all` | `all` |
| `terminal_branch_buffered` | `all` | `terminal_branch_buffered` |
| `terminal_leaf_unbuffered` | `all` | `terminal_leaf_unbuffered` |

Therefore `all` is an implicit dependency for every non-empty request. The request builder should normalize every request by adding `all`.

Base length construction follows the same semantic split:

- `all`: all raw `SegmentChar` samples for that length.
- `terminal_branch_buffered`: raw samples whose `BufferingPattern::hasTerminalBranchBuffer()` is true.
- `terminal_leaf_unbuffered`: raw samples whose terminal semantic is leaf-unbuffered.

## Demand Planning

Add small helpers near `segment_pruning` or `topology_pruning`:

- `MakeSourceTrunkSegmentFrontierRequest(length_idx)`: returns `all`.
- `MakeHTreeSegmentFrontierRequest(levels, boundary_constraints, profile)`: returns `all` unless branch forcing requires `all | terminal_branch_buffered`; optional full profile returns all three kinds.

Recommended rollout:

1. First implementation slice preserves downstream HTree full synthesis while migrating APIs. This removes the source-trunk-specific patch and establishes the catalog boundary with minimal behavior risk.
2. Second implementation slice switches downstream HTree to demand-derived requests after equivalence tests are in place. This is where unrestricted HTree can skip branch/leaf construction and branch-forced HTree can skip leaf construction.

## Readability Refactor Candidates

Low-risk, recommended in this task:

- Move `CountSegmentFrontierEntries` into the new catalog instead of duplicating storage knowledge in `HTree.cc`.
- Extract selected pattern decoration from `HTree::build` into a helper such as `ApplySelectedPatternToLevelPlans(...)`.
- Extract `HTree::build` orchestration chunks into named helpers: input validation, topology build, characterization setup, segment frontier synthesis, depth search, global selection, selected legality/root-driver resolution, embedding, and summary emission.
- Split `SegmentLibrary.hh` into narrower headers if compile dependencies remain reasonable: `SegmentFrontierTypes.hh`, `SegmentPatternLibrary.hh`, and `TopologyPatternLibrary.hh`.
- Rename stale file comments such as `SegmentFrontier.cc`/`PatternLibrary.hh` comments where the actual filenames are `SegmentPruning.cc` and `SegmentLibrary.hh`.

Moderate-risk, defer unless needed:

- Replace raw `CandidateCharRef` pointers with stable `{candidate_index, pool_kind, entry_index}` references to make lifetime safety explicit.
- Extract a reusable delay/power Pareto helper shared by `TopologyPruning.cc` and `SourceTrunkSegment.cc`.
- Extract common buffer/net object construction between HTree embedding and source trunk object construction.

High-risk, out of first slice:

- Split `HTree::BuildResult` into planning result, selected solution, and object ownership result.
- Change topology search, compensation, sink-load-region legality, or CharBuilder behavior.

## Correctness Contract

For the initial refactor, source trunk and downstream HTree should produce the same selected physical solution as the predecessor runtime task.

If narrower HTree demand changes internal pattern ID allocation, validation should compare:

- selected depth;
- materialized level segment buffer positions and cell masters;
- terminal branch-buffer flags per selected level;
- selected delay/power and raw HTree metric;
- selected root driver and physical root load;
- final buffer count and wirelength;
- setup and hold WNS for the target flow.

Pattern IDs may be kept stable during the first slice by preserving full synthesis for downstream HTree. If the second slice enables narrower HTree requests, pattern ID drift must be called out explicitly and covered by materialized-pattern equivalence.

## Rollout / Rollback

- Start only after the predecessor runtime optimization task is committed or otherwise isolated.
- Keep the first slice as API/structure preserving behavior.
- Add tests before switching downstream HTree to narrower demand.
- Roll back the second slice if selected physical topology or QoR drifts without an explicit acceptance decision.
