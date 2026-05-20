# CTS convergence audit research

## Current Task State

- Active task: `.trellis/tasks/05-20-cts-code-normalization-convergence-audit-correction`.
- Status: `in_progress`.
- Previous parent task: `.trellis/tasks/05-19-cts-code-normalization-refactor-research`, marked `completed`.
- Current branch: `cts_refactor`.
- Working tree remains dirty from the previous iCTS normalization campaign and this convergence implementation. No commit has been made for this
  task.

## Audit Commands

The initial audit used source inventory and include scans rather than relying only on subjective naming review:

```bash
find src/operation/iCTS/source -type d | while read -r d; do
  f=$(find "$d" -maxdepth 1 -type f \( -name '*.hh' -o -name '*.cc' \) | wc -l)
  h=$(find "$d" -maxdepth 1 -type f -name '*.hh' | wc -l)
  sub=$(find "$d" -mindepth 1 -maxdepth 1 -type d | wc -l)
  if [ "$f" -ge 6 ] || [ "$h" -ge 3 ]; then
    printf '%3d files %3d headers %2d dirs :: %s\n' "$f" "$h" "$sub" "$d"
  fi
done | sort -nr
```

```bash
rg -n '#include "FastSta(Clock|Segment|Ids|Liberty|Parasitic|Timing|Power|Dmp|Builder|Char|Incremental|Report)' \
  src/operation/iCTS/source src/operation/iCTS/test -g '*.cc' -g '*.hh'
```

```bash
rg -n '#include "(CharBuilder|Frontier|HTreeTopologyCharTable|HTreeTraits|HashJoinEngine|PatternCombiner|SegmentCharTable|SegmentTraits|CharacterizationBufferCell|CharBuilderSweepState)' \
  src/operation/iCTS/source src/operation/iCTS/test -g '*.cc' -g '*.hh'
```

```bash
rg -n '#include "(BSTRouter|BoundSkewTree|BSTRoutingConfig|BstClockTreeConversion|Components|GeomCalc)' \
  src/operation/iCTS/source src/operation/iCTS/test -g '*.cc' -g '*.hh'
```

## Direct Directory Inventory

The following directories are the main convergence candidates by direct file count and exposed root headers:

| Directory | Direct files | Direct headers | Child dirs | Initial classification |
| --- | ---: | ---: | ---: | --- |
| `source/module/characterization` | 22 | 10 | 0 | Primary fix |
| `source/module/routing/bound_skew_tree` | 21 | 6 | 0 | Primary fix |
| `source/flow/synthesis/htree/analytical_solver` | 14 | 5 | 0 | Secondary fix or split |
| `source/database/adapter/sdc` | 14 | 5 | 0 | Secondary fix or split |
| `source/database/design` | 12 | 8 | 0 | Audit as data-model exception |
| `source/module/topology/fast_clustering` | 12 | 2 | 0 | Secondary fix or split |
| `source/flow/synthesis/htree/solution` | 10 | 5 | 0 | Secondary fix or naming review |
| `source/database/adapter/sta` | 10 | 2 | 0 | Secondary fix or split |
| `source/database/adapter/fast_sta` | 9 | 8 | 9 | Primary root-contract violation |
| `source/database/adapter/fast_sta/timing` | 8 | 3 | 0 | Internal FastSTA timing split candidate |
| `source/database/characterization` | 7 | 7 | 0 | Audit as data-model exception |
| `source/flow/synthesis/htree/segment_pruning` | 7 | 6 | 0 | Secondary naming/contract review |
| `source/database/io` | 6 | 2 | 0 | Primary naming correction |

## FastSTA Findings

Current root files under `src/operation/iCTS/source/database/adapter/fast_sta`:

```text
FastSta.cc
FastSta.hh
FastStaClockAnalysis.hh
FastStaClockNetParasitic.hh
FastStaClockPower.hh
FastStaClockSizingEdit.hh
FastStaClockTiming.hh
FastStaIds.hh
FastStaSegmentCharacterization.hh
```

This violates the user-stated target shape because the root still exposes seven helper headers besides the `FastSta` external contract.

Current subfolders:

```text
clock_net_parasitic/
clock_sizing/
clock_state/
clock_tree/
liberty/
power/
report/
segment_char/
timing/
```

The previous split created meaningful subfolders, but the root helper headers and CMake visibility still make implementation contracts available as
general include targets.

Current external production users include root FastSTA helper headers directly:

```text
source/flow/optimization/candidate/OptimizationCandidates.cc
source/flow/optimization/candidate/OptimizationCandidates.hh
source/flow/optimization/candidate/OptimizationScalableCandidates.cc
source/flow/optimization/preparation/OptimizationPreparation.cc
source/flow/optimization/preparation/OptimizationPreparation.hh
source/flow/optimization/report/OptimizationReport.cc
source/flow/optimization/solver/OptimizationSolver.cc
source/flow/optimization/solver/OptimizationSolver.hh
source/flow/optimization/state/OptimizationState.cc
source/flow/optimization/state/OptimizationState.hh
source/module/characterization/CharBuilder.hh
source/module/characterization/CharBuilderCircuit.cc
source/module/characterization/CharBuilderSlewSampling.cc
```

The main dependency categories being pulled out of FastSTA are:

- clock timing observations;
- clock power observations;
- clock-net parasitic values;
- clock sizing edit descriptions;
- FastSTA id types;
- segment characterization sample data.

The correction should either:

- make those concepts methods or nested contract types reachable through `FastSta.hh`; or
- move stable CTS data contracts to an appropriate database/domain location, then keep FastSTA implementation details private.

Test `FastSTATest.cc` also includes many internal headers. That can remain a component-test concern only if test target include visibility is kept
separate from production source visibility.

## `database/io` Findings

Current files under `src/operation/iCTS/source/database/io`:

```text
CMakeLists.txt
ClockIdbPinMembership.cc
ClockIdbWritebackData.hh
Wrapper.cc
Wrapper.hh
WrapperClockReader.cc
WrapperClockWriter.cc
```

User specifically rejected the new visible terms `Writeback` and `Membership` in this directory.

The rejected helper header currently contains:

- `IdbNetPinMembership`
- `ClockIdbWritebackScope`
- `ClockIdbWritebackRestoreData`
- helper functions for iDB pin attachment, pin clearing, pin lookup, and net pin capture.

Actual business semantics are writer-owned iDB clock routing materialization and preservation of pre-existing iDB net/instance connections while
the CTS clock tree is emitted. The current names describe a general engineering operation instead of the `WrapperClockWriter` responsibility.

Recommended correction:

- keep visible root files close to the previous shape: `Wrapper.hh`, `Wrapper.cc`, `WrapperClockReader.cc`, `WrapperClockWriter.cc`;
- move helper declarations into `WrapperClockWriter.cc` local scope when feasible;
- if the writer logic must remain split, place it under a writer-specific subfolder and use CTS/iDB clock-tree terms approved by the user, not
  root-level `Writeback` or `Membership`.

## `module/characterization` Findings

Current root files:

```text
CharBuilder.cc
CharBuilder.hh
CharBuilderBuild.cc
CharBuilderCircuit.cc
CharBuilderConfig.cc
CharBuilderFeasibility.cc
CharBuilderPatternEnumeration.cc
CharBuilderPatternStorage.cc
CharBuilderSampleStorage.cc
CharBuilderSampling.cc
CharBuilderSlewSampling.cc
CharBuilderStaSampling.cc
CharBuilderSweepState.hh
CharBuilderTopology.cc
CharacterizationBufferCell.hh
Frontier.hh
HTreeTopologyCharTable.hh
HTreeTraits.hh
HashJoinEngine.hh
PatternCombiner.hh
SegmentCharTable.hh
SegmentTraits.hh
```

Issues:

- The directory has 22 direct source/header files and no child directories.
- `CharBuilder.hh` is an external contract but also exposes FastSTA id types through `FastStaIds.hh`.
- H-tree synthesis code includes characterization table and pruning helpers directly:
  - `Frontier.hh`
  - `HashJoinEngine.hh`
  - `HTreeTraits.hh`
  - `SegmentTraits.hh`
- `CharacterizationBufferCell.hh` is used by `module/analytical_characterization`, H-tree characterization library code, and tests. It may be a
  true public characterization data contract, but it should not force all builder internals to stay at the directory root.

Likely responsibility groups:

- builder entry and options: `CharBuilder.hh/.cc`;
- characterization circuit construction and removal;
- wire/slew/load sampling;
- pattern enumeration and storage;
- feasibility checks;
- characterization tables and traits used by H-tree pruning;
- buffer-cell electrical limits and pin roles.

The key design question is whether `CharacterizationBufferCell` remains a public characterization contract at the root, becomes part of
`CharBuilder.hh`, or moves to a more stable database/domain location.

## `module/routing/bound_skew_tree` Findings

Current root files:

```text
BSTRouter.cc
BSTRouter.hh
BSTRouterBinaryTopology.cc
BSTRouterExport.cc
BSTRoutingConfig.hh
BoundSkewTree.cc
BoundSkewTree.hh
BoundSkewTreeBalance.cc
BoundSkewTreeEmbedding.cc
BoundSkewTreeFlow.cc
BoundSkewTreeInfeasibleMerge.cc
BoundSkewTreeJoining.cc
BoundSkewTreeTopology.cc
BstClockTreeConversion.hh
Components.cc
Components.hh
GeomCalc.cc
GeomCalc.hh
GeomCalcLine.cc
GeomCalcPointRegion.cc
GeomCalcTransformedRect.cc
```

Issues:

- The directory has 21 direct files and no child directories.
- `BSTRouter.hh` appears to be the external routing entry used by `concurrent_bst_salt/CBSRouter.cc`.
- `BoundSkewTree.hh`, `Components.hh`, and `GeomCalc.hh` form the internal algorithm and geometry working set but are all exposed at root.
- `BoundSkewTree.hh` includes `BSTRoutingConfig.hh`, `Components.hh`, and `GeomCalc.hh`, which makes geometry and component types part of the
  visible root contract.

Likely responsibility groups:

- router entry: `BSTRouter.hh/.cc`;
- tree construction and merge/join/embed flow;
- geometry calculation;
- routing components;
- route-tree conversion;
- routing configuration.

Recommended shape:

- keep `BSTRouter.hh/.cc` as the root external contract unless a separate public `BoundSkewTree` contract is required;
- move tree algorithm files under a tree-construction subfolder;
- move geometry files under a geometry subfolder;
- move route-tree conversion under a clock-tree conversion subfolder;
- keep external callers from depending on `Components.hh` and `GeomCalc.hh` directly.

## Secondary Directory Findings

### `flow/synthesis/htree/analytical_solver`

Current root files include candidate data, solver options/results, model flow, shortlist trimming, validation, and problem construction. It has
14 direct files and 5 direct headers.

`AnalyticalSolver.hh` is the likely external contract, but it includes `AnalyticalCandidate.hh` and exposes many result details. The implementation
also has a new `AnalyticalHTreeCandidateSearch.hh` and `AnalyticalSolveProblem.cc`. This directory should be reviewed after primary fixes because
it is a behavior module with a clear solve entry.

### `database/adapter/sdc`

Current root files include SDC clock parsing, clock command evaluation, clock model values, and clock trace resolution. It has 14 direct files and
5 direct headers.

Initial review considered multiple adapter entries, but the user later required strict external uniqueness for this directory. Final shape keeps only
`SdcClockReader.hh/.cc` at root; parser and clock-trace implementation live under `clock_parser/` and `clock_trace/`.

### `module/topology/fast_clustering`

Current root files include clustering entry, boundary search/polish, merge polish, partition, geometry, finalization, and `FastClusteringDraft.hh`.
The external source include appears to be only `FastClustering.hh`; `FastClusteringDraft.hh` is internal working data.

This is a good secondary candidate for root-contract cleanup: keep `FastClustering.hh/.cc` at root and move working data plus algorithm parts into
subfolders.

### `database/adapter/sta`

The external adapter contract is `STAAdapter.hh`, while `STAAdapterTimingQuery.hh` was introduced as a helper header. Current source files use it
within the STA adapter only. This may be fixed by moving timing-query helpers into an internal subfolder or making them `.cc`-local if possible.

### `flow/synthesis/htree/solution`

The directory has 10 direct files and 5 headers, with generic names such as `Solution`, `SolutionReport`, `SolutionSelection`, and `StageReport`.
This should be reviewed in the final naming pass because the structure may be acceptable only after the H-tree domain terms are made more explicit.

## Data-Model Exception Candidates

The following directories should be audited, but they may be valid exceptions to a strict root-only contract rule:

- `source/database/design`: 12 direct files, 8 headers. These are stable clock design/domain objects, including `ClockNetwork.hh`.
- `source/database/characterization`: 7 direct files, 7 headers. These are characterization data model objects.
- `source/database/spatial` and `source/database/routing`: smaller data-model directories with multiple headers.

The proposed rule is that such directories can expose multiple root headers only when each header names a stable CTS domain object and is not an
implementation helper for a behavior module.

## Naming Findings

Current path-level naming concerns confirmed by user feedback:

- `ClockIdbWritebackData.hh`
- `ClockIdbPinMembership.cc`

The previous task already updated the backend naming spec to ban broad copied-state and vague structural terms in source naming. This new task adds
the user feedback that `Writeback` and `Membership` are also not acceptable for the `database/io` visible shape.

Current textual scan for the broad terms found only non-structural or existing report/comment uses:

- `database/design/Clock.hh`: comment text "Membership helpers."
- QoR report titles containing "Root Input".
- one FlowTest assertion checking absence of "Input Overview".
- characterization log table column "Input Cap".

These are not necessarily file/type/module names. The final source naming pass should distinguish structural names from report text and comments.

## Later New-File Naming Review

Do not run the final rename pass until folder structure is stable. At that time, classify files using git status and rename detection:

- true newly introduced files;
- files moved from previous locations;
- files moved and substantially edited;
- test-only helper files.

Current untracked source candidates that may need review include root FastSTA helper headers, `ClockIdbWritebackData.hh`,
`ClockIdbPinMembership.cc`, new H-tree analytical solver files, FastClustering working data, and new characterization/routing helper headers.

This list is intentionally not final because several files are moved responsibilities from the previous layout and should not be treated as new
names until the structure is converged.

## Convergence Implementation Evidence

The structural convergence pass has corrected the root-contract violations called out by the user:

- `database/adapter/fast_sta` root now contains only `CMakeLists.txt`, `FastSta.cc`, and `FastSta.hh`.
- `database/adapter/sdc` root now contains only `CMakeLists.txt`, `SdcClockReader.cc`, and `SdcClockReader.hh`; parser and trace code are under
  `clock_parser` and `clock_trace`.
- `database/io` root now contains only `CMakeLists.txt`, `Wrapper.cc`, `Wrapper.hh`, `WrapperClockReader.cc`, and `WrapperClockWriter.cc`.
- `module/characterization` root now contains only `CMakeLists.txt`, `Characterization.cc`, and `Characterization.hh`; builder, buffer-cell,
  pattern, pruning, sampling, table, and circuit files are under responsibility subfolders.
- `module/routing/bound_skew_tree` root now contains only `CMakeLists.txt`, `BSTRouter.cc`, and `BSTRouter.hh`; config, tree, geometry, component,
  and clock-tree conversion files are under responsibility subfolders.
- `module/topology/fast_clustering` root now contains only `CMakeLists.txt`, `FastClustering.cc`, and `FastClustering.hh`.
- `flow/synthesis/htree/analytical_solver` root now contains only `CMakeLists.txt`, `AnalyticalSolver.cc`, and `AnalyticalSolver.hh`.
- `flow/synthesis/htree/solution` root now contains only `CMakeLists.txt`, `Solution.cc`, and `Solution.hh`.
- `database/adapter/sta` root now contains only `CMakeLists.txt`, `STAAdapter.cc`, and `STAAdapter.hh`; implementation slices are grouped under
  `cell_master`, `clock_lookup`, `net_rc`, `net_rc_tree`, `root_driver`, `timing_query`, and `timing_update`.

Focused builds completed successfully for the moved targets:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_test_database_adapter_fast_sta
ninja -C build icts_source_database_io icts_test_flow
ninja -C build icts_source_module_characterization icts_source_module_routing_bst
ninja -C build icts_test_module_characterization icts_test_module_analytical_characterization icts_test_module_routing \
  icts_test_flow_synthesis_htree icts_test_flow_synthesis_htree_analytical_solver icts_test_flow
ninja -C build icts_source_module_topology_fast_clustering icts_source_database_adapter_sta \
  icts_source_database_adapter_sdc icts_source_database_adapter_fast_sta_liberty
ninja -C build icts_source_flow_synthesis_htree_analytical_solver icts_test_flow_synthesis_htree_analytical_solver \
  icts_source_flow_synthesis_htree_solution icts_test_flow_synthesis_htree
```

The final local `STAAdapter` move was additionally validated with:

```bash
ninja -C build icts_source_database_adapter_sta
```

The later strict-facade correction for SDC, characterization, and bound-skew tree routing was validated with:

```bash
ninja -C build icts_source_module_characterization icts_source_module_routing_bst icts_source_module_routing \
  icts_source_module_topology_cluster_constraints icts_source_database_adapter_sdc icts_test_module_characterization \
  icts_test_module_routing icts_test_flow_synthesis_htree icts_test_flow_synthesis_htree_analytical_solver icts_test_flow
```

The current source include scan found no production source including the hidden FastSTA helper headers, characterization implementation subfolder
headers, BST config/geometry/component/tree internals, SDC parser/trace internals, or STA adapter subfolder paths as broad external contracts.

The redundant `source/module/routing/database` forwarding target was removed. Routing module CMake files now link `icts_source_database_routing`
directly.

All `.md` files under `src/operation/iCTS` were removed per the convergence instruction to keep CTS directory docs out of the source tree for now.

## Accepted Root-Header Exceptions

The remaining directories with multiple direct root headers are accepted only where the headers are stable CTS data contracts or explicit public
subflow contracts:

- `database/design`: stable CTS design objects and views such as `Clock`, `ClockNetwork`, `ClockLayout`, `Design`, `Inst`, `Net`, and `Pin`.
- `database/characterization`: stable characterization data objects such as `BufferingPattern`, `CharCore`, `HTreeTopologyChar`, `PatternId`,
  `SegmentChar`, and `ValueLattice`.
- `database/routing`: routing database objects such as `ClockRouteSegmentRc`, `RoutingTerminal`, and `SteinerTree`.
- `database/spatial`: geometry data objects such as `Point`, `Rect`, `Region`, and `Tree`.
- `flow/synthesis/htree`: `HTree`, `HTreeSynthesisOptions`, and `HTreeSynthesisResult` are public H-tree synthesis contracts used by topology,
  trace, plan, compensation, and solution subflows.
- `flow/synthesis/htree/segment_pruning`: the segment frontier and pattern-library headers are shared H-tree synthesis contracts rather than one
  directory's private helper headers. Their final names still belong to the user-reviewed naming pass.
- `flow/synthesis/htree/embedding`: `Embedding`, `EmbeddingState`, and `BufferPortTable` are shared H-tree embedding contracts used by source-trunk
  and analytical-solution code. Their naming can be revisited with the broader H-tree naming list if desired.
- `flow/synthesis/trace/layout`: `ClockLayoutAdapter`, `ClockLayoutBuilder`, and `ClockLayoutSynthesisTopology` are trace-layout public contracts.
  `ClockLayoutSynthesisTopology` remains in the naming-review list because the name may be too broad.
- `module/analytical_characterization`: `AnalyticalCharacterization`, `AnalyticalFit`, and `AnalyticalModel` are public analytical
  characterization contracts. They are below the large-flat-directory threshold and are not split in this pass.

## Current Naming Scan

The current iCTS source scan for rejected structural terms reports only documented exceptions or non-structural display strings:

- `Input`: table/report display labels such as "Root Input" and "Input Cap".
- `Membership`: stable `Clock` / `Design` domain membership methods for final CTS design objects.
- no remaining source matches for `snapshot`, `Internal`, `Support`, `Request`, `Response`, `Types`, `rollback`, `fallback`, `Session`,
  `Writeback`, or `writeback`.

## Final Validation Evidence

Final structural scans completed after the strict facade and IWYU convergence:

```bash
find src/operation/iCTS -type f -name '*.md'
```

No CTS markdown files remain.

```bash
rg -n 'icts_source_module_routing_database|module/routing/database|routing/database' \
  src/operation/iCTS/source src/operation/iCTS/test -g '*.cc' -g '*.hh' -g 'CMakeLists.txt'
```

No redundant `module/routing/database` references remain.

```bash
rg -n '#include "(adapter/sdc/(?!SdcClockReader\.hh)|database/adapter/sdc/(?!SdcClockReader\.hh)|characterization/(CharBuilder|CharacterizationBufferCell|(builder|buffer_cell|pruning|pattern|table)/)|bound_skew_tree/(BSTRoutingConfig|(config|tree|geometry|component)/))' \
  src/operation/iCTS/source src/operation/iCTS/test -g '*.cc' -g '*.hh' --pcre2 \
| rg -v 'src/operation/iCTS/source/(module/characterization|module/routing/bound_skew_tree)/'
```

No external include bypasses remain for SDC, characterization, or bound-skew tree strict facade roots.

The final rejected-term scan reports only non-structural report strings or accepted design-object terminology:

- `Input`: report/table strings such as `Root Input` and `Input Cap`.
- `Membership`: stable `Clock` / `Design` domain membership methods and comments.
- no remaining source matches for `snapshot`, `Internal`, `Support`, `Request`, `Response`, `Types`, `rollback`, `fallback`, `Session`,
  `Writeback`, or `writeback`.

Final source quality validation passed:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result:

- `format`: 0 in-scope findings.
- `tidy`: 0 in-scope findings.
- `headers`: 0 in-scope findings.
- `cmake`: 0 in-scope findings.
- `iwyu`: 0 in-scope findings.

Runtime validation passed:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Result:

- `iCTS run successfully.`
- CTS runtime overview total: `16.154 s`.
- Report stage finished and generated DEF, Verilog, statistics, metrics, SVG, and GDS outputs.

During final IWYU convergence, the characterization and bound-skew tree targets were tightened further: child subdirectories are not broad include
roots, and internal source uses rooted CTS paths such as `characterization/builder/CharBuilder.hh` and
`bound_skew_tree/tree/BoundSkewTree.hh`. Facade headers still remain the required external include site.
