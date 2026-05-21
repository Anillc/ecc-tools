# W8 Baseline Counts & Outcomes

> Snapshot taken at the start of Wave 8 (2026-05-20) and updated with after-state.

## W8a — Large header reduction

### Before
Files **> 300 lines**:
- `database/routing/SteinerTree.hh` (355) — exempt (template-only, all-inline)
- `flow/report/visualization/core/SvgRenderingPrimitives.hh` (330) — exempt (SVG inline hot-path consts + helpers)
- `module/characterization/ParetoFront.hh` (326)

### After
Files **> 300 lines** (2 remaining, both with documented exemption notices):
- `database/routing/SteinerTree.hh` (355) — unchanged, exemption preserved
- `flow/report/visualization/core/SvgRenderingPrimitives.hh` (330) — unchanged, exemption preserved
- ~~`module/characterization/ParetoFront.hh` (326)~~ → **277 lines** (Sort helpers moved out)

### Action
- Created `module/characterization/ParetoFront.cc` (new) containing:
  - `SortSegmentFrontierEntries(std::vector<SegmentChar>&)` (non-template, was inline)
  - `SortHTreeFrontierEntries(std::vector<HTreeTopologyChar>&)` (non-template, was inline)
- Forward-declared both in `ParetoFront.hh`; rest of file (template Pareto-pruner core) stayed inline as required.
- Updated `module/characterization/CMakeLists.txt` to add `ParetoFront.cc`.
- Replaced exemption notice to describe the new split state.

## W8b — fast_sta/types/ consolidation

### Before (12 + Fwd + TypesCc = 14 files)
```
FastStaCharTypes.hh         FastStaLibertyTypes.hh
FastStaContext.hh           FastStaNodeNet.hh
FastStaEnums.hh             FastStaParasiticTypes.hh
FastStaGeometry.hh          FastStaPowerTypes.hh
FastStaIds.hh               FastStaStatusTypes.hh
FastStaIncrementalTypes.hh  FastStaTimingTypes.hh
FastStaTypesFwd.hh          FastStaTypes.cc
```

### After (5 headers + 1 .cc)
```
FastStaCore.hh          (NEW)  ← Ids + Enums + StatusTypes + TypesFwd
FastStaGeometry.hh             ← Geometry + NodeNet (expanded)
FastStaTimingTypes.hh          ← TimingTypes + ParasiticTypes + IncrementalTypes (expanded)
FastStaLibertyPower.hh  (NEW)  ← LibertyTypes + PowerTypes
FastStaContext.hh              ← Context + CharTypes (expanded)
FastStaTypes.cc                ← (unchanged)
```

Deleted: `FastStaIds.hh`, `FastStaEnums.hh`, `FastStaStatusTypes.hh`, `FastStaTypesFwd.hh`, `FastStaNodeNet.hh`, `FastStaIncrementalTypes.hh`, `FastStaParasiticTypes.hh`, `FastStaLibertyTypes.hh`, `FastStaPowerTypes.hh`, `FastStaCharTypes.hh` (10 files).

### Action
- 5 new consolidated headers written.
- Bulk sed across 47 consumer files: `FastStaXxx.hh` includes → consolidated header equivalents.
- Bulk sed for the 1 qualified path `adapter/fast_sta/types/FastStaIds.hh` → `adapter/fast_sta/types/FastStaCore.hh`.
- Deduplicated consecutive duplicate `#include "FastStaXxx.hh"` lines in 7 files (introduced by aliasing multiple old headers to the same new header).
- `FastStaTypes.cc` already migrated by sed; verified its includes now reference `FastStaCore.hh` + `FastStaLibertyPower.hh`.

## W8c — htree subdirectory facade cleanup

### solution/ — moved 4 contracts to detail/
```
Before                          After
solution/AnalyticalSolution.hh  solution/detail/AnalyticalSolution.hh
solution/SolutionReport.hh      solution/detail/SolutionReport.hh
solution/SolutionSelection.hh   solution/detail/SolutionSelection.hh
solution/StageReport.hh         solution/detail/StageReport.hh
solution/Solution.hh (24L, doc-only)  → upgraded to true facade (transitively includes 4 detail headers)
```
Consumer paths updated in `HTree.cc`, `AnalyticalSolution.cc`, `SolutionReport.cc`, `SolutionSelection.cc`, `StageReport.cc`.

### embedding/ — moved 1 internal state header to detail/
```
Before                          After
embedding/Embedding.hh          embedding/Embedding.hh           (facade — unchanged)
embedding/BufferPortTable.hh    embedding/BufferPortTable.hh     (kept public — consumed by topology/trunk/SourceTrunkSegment.cc)
embedding/EmbeddingState.hh     embedding/detail/EmbeddingState.hh
```
Consumer paths updated in `Embedding.cc`.

### segment_pruning/ — kept as-is
6 type headers (`SegmentLibrary` umbrella + `BufferPatternLibrary` + `BufferStrength` + `PatternLibraryCombiner` + `SegmentFrontier` + `TopologyPatternLibrary`) all have 7-18 external consumers across `analytical_solver/`, `topology_pruning/`, `region/`, `compensation/`, `flow/synthesis/topology/`, and tests. Moving them to `detail/` would break encapsulation. Left in place.

### analytical_solver/ — no further moves
`AnalyticalSolverDetail.hh` already exists (from W3c). `AnalyticalCandidate.hh` / `AnalyticalSelection.hh` / `AnalyticalValidation.hh` have cross-file solver-stage consumers; cannot demote.

### plan/ — left as-is
Only 2 .hh files (`Plan.hh` + `DepthPlan.hh`), minimal facade pair.

## W8d — module/characterization top-level

```
116 CharBuilder.hh          (facade — keep)
277 ParetoFront.hh          (W8a — now under 300)
206 HashJoinConcat.hh       (template helper — keep)
116 CharBuilder.hh          (facade — keep)
 96 HTreeTopologyCharTable.hh
 95 SegmentCharTable.hh
 72 HTreeTraits.hh
 71 PatternCombiner.hh
 63 SegmentTraits.hh
 24 Frontier.hh             (compat forwarder — keep)
```

All other headers are small (≤ 206 lines), public consumer-facing types (table records, traits CRTPs, template combiners) or compat forwarders. No aggressive `detail/` move — would only add include hop without isolation benefit.

## W8e — detail namespace audit (read-only)

96 occurrences of `namespace .* detail` across 48 files. Distribution:

| Location | Files | Disposition |
|---|---:|---|
| `module/characterization/detail/` | 18 | ✓ physically isolated |
| `module/topology/fast_clustering/detail/` | 15 | ✓ physically isolated |
| `module/routing/bound_skew_tree/algorithm/detail/` | 14 | ✓ physically isolated |
| `database/adapter/sta/detail/` | 13 | ✓ physically isolated |
| `database/adapter/fast_sta/dmp_ceff/` (no `detail/` subdir) | 8 | sub-dir itself is implementation layer for `FastStaDmpCeff.hh` facade — acceptable |
| `flow/optimization/*` (model / candidate / state / options / preparation / report / mutation / solver) | 18 | uses `icts::optimization::detail` widely without physical isolation — **anti-pattern but out of W8 scope**; restructuring would require reorganizing whole optimization subdir |
| `module/characterization/CharBuilder.hh` | 1 | forward-declare of detail/ Pimpl — compliant |
| `module/characterization/HashJoinConcat.hh` | 1 | single-file template helper scoping — acceptable |
| `utils/logger/LogFormat.hh` | 1 | single-file scoping — acceptable |

W8e action: **no code change** — clean isolation in 4 detail/ dirs; remaining usages either acceptable single-file scoping or out-of-scope anti-pattern documented for future cleanup.

## Build verification

- After each W8 sub-step: `bash build.sh` exit 0.
- Final build: 47 `icts_test_*` binaries linked, `iEDA` fresh at 2026-05-20 20:01.
- Headers > 300 lines: **2** (both with exemption notices) — meets W8 target ≤ 2.
- fast_sta/types/ headers: **5** — meets W8 target 4-5.
- htree solution/embedding facade-vs-detail split clear.
