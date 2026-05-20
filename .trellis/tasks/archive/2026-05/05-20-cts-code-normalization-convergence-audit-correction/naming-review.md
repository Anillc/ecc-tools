# CTS convergence naming review

## Scope

This file is the final naming-review gate requested by the user after folder convergence. It separates files that were only moved from files that
are new, renamed, or contract-extracted enough to need user direction before further renaming.

The inventory was generated from current deleted tracked files and untracked source files under `src/operation/iCTS/source`.

## Pure Moves Or Directory Relocations

These are not treated as newly named files because their basenames match deleted tracked files and the current work mainly changed their directory
ownership:

- FastSTA implementation slices under `clock_net_parasitic`, `clock_sizing`, `clock_state`, `clock_tree`, `liberty`, `power`, `report`,
  `segment_char`, and `timing`.
- SDC parser and trace implementation slices under `clock_parser` and `clock_trace`.
- STA adapter implementation slices under `cell_master`, `clock_lookup`, `net_rc`, `net_rc_tree`, `root_driver`, and `timing_update`.
- H-tree analytical solver moved files under `candidate`, `model`, and `selection`.
- H-tree solution moved files under `analytical`, `selection`, and `report`.
- Characterization builder, circuit, pattern, pruning, sampling, and table files moved under responsibility folders.
- Bound-skew tree router implementation, tree, component, geometry, and clock-tree conversion files moved under responsibility folders.
- Fast clustering boundary, finalize, geometry, partition, polish, and docs files moved under responsibility folders.

## True New Or Renamed Source Names

These names should be reviewed before applying more renames. They are grouped by CTS responsibility.

### FastSTA Adapter

- `database/adapter/fast_sta/clock_net_parasitic/FastStaClockNetParasitic.hh`
- `database/adapter/fast_sta/clock_sizing/FastStaClockSizingEdit.hh`
- `database/adapter/fast_sta/clock_state/FastStaClockState.hh`
- `database/adapter/fast_sta/liberty/FastStaLibertyModel.cc`
- `database/adapter/fast_sta/liberty/FastStaLibertyModel.hh`
- `database/adapter/fast_sta/timing/FastStaClockTiming.hh`
- `database/adapter/fast_sta/timing/FastStaDmpCeffSolver.hh`

Naming notes:

- `ClockNetParasitic`, `ClockSizingEdit`, and `ClockTiming` are CTS/timing-domain terms and look acceptable unless the user wants shorter names.
- `ClockState`, `LibertyModel`, and `DmpCeffSolver` contain more generic words and may need user direction.

### External Adapter Boundaries

- `database/adapter/sdc/clock_trace/SdcClockTraceAlgorithm.hh`
- `database/adapter/sta/timing_query/STAAdapterTimingQuery.cc`
- `database/adapter/sta/timing_query/STAAdapterTimingQuery.hh`

Naming notes:

- `Algorithm` and `Query` are generic structural words. They should be confirmed or replaced with more explicit SDC/iSTA clock timing semantics.

### Shared Database Contracts

- `database/routing/ClockRouteSegmentRc.hh`

Naming notes:

- This is a CTS routing RC data contract and looks business-specific.

### Flow Contracts

- `flow/evaluation/qor/ClockQorMetricCollector.hh`
- `flow/optimization/clock_sizing_edit/ClockSizingAcceptedEdit.cc`
- `flow/optimization/clock_sizing_edit/ClockSizingAcceptedEdit.hh`
- `flow/optimization/model/ClockSizingOptimizationData.hh`
- `flow/setup/clock_data/ClockDataRead.cc`
- `flow/setup/clock_data/ClockDataRead.hh`
- `flow/synthesis/htree/HTreeSynthesisOptions.hh`
- `flow/synthesis/htree/HTreeSynthesisResult.hh`
- `flow/synthesis/htree/analytical_solver/candidate/AnalyticalHTreeCandidateSearch.hh`
- `flow/synthesis/htree/analytical_solver/model/AnalyticalSolveProblem.cc`
- `flow/synthesis/htree/compensation/RootDriverCompensationState.hh`
- `flow/synthesis/htree/segment_pruning/SegmentFrontierCatalog.hh`
- `flow/synthesis/htree/segment_pruning/SegmentPatternLibrary.hh`
- `flow/synthesis/htree/segment_pruning/TopologyPatternLibrary.hh`
- `flow/synthesis/trace/layout/ClockLayoutSynthesisTopology.hh`

Naming notes:

- `ClockDataRead` is under the user-confirmed `setup/clock_data` boundary and looks acceptable unless the action should be named more narrowly.
- `ClockSizingAcceptedEdit` is CTS-specific and likely acceptable.
- `MetricCollector`, `OptimizationData`, `Options`, `Result`, `Search`, `Problem`, `State`, `Catalog`, and `Library` are generic enough to need
  confirmation.
- `ClockLayoutSynthesisTopology` may be too broad because it combines layout, synthesis, and topology vocabulary.

### Module Contracts

- `module/characterization/Characterization.hh`
- `module/characterization/Characterization.cc`
- `module/characterization/buffer_cell/CharacterizationBufferCell.hh`
- `module/characterization/builder/CharBuilderSweepState.hh`
- `module/routing/bound_skew_tree/config/BSTRoutingConfig.hh`
- `module/routing/bound_skew_tree/clock_tree_conversion/BstClockTreeConversion.hh`
- `module/topology/cluster_constraints/ClusterConstraintEvaluation.hh`
- `module/topology/fast_clustering/cluster_draft/FastClusteringDraft.hh`

Naming notes:

- `Characterization`, `CharacterizationBufferCell`, `BSTRoutingConfig`, `BstClockTreeConversion`, and `ClusterConstraintEvaluation` are
  CTS/module-specific and user-confirmed acceptable.
- `SweepState` and `Draft` are generic structural terms and should be confirmed or renamed.

## Current Banned-Term Scan

Current source scan has no structural files/types containing:

```text
snapshot
Internal
Support
Request
Response
Types
rollback
fallback
Session
Writeback
writeback
```

Remaining matches are documented exceptions:

- `Input`: display/report table strings such as "Root Input" and "Input Cap".
- `Membership`: stable CTS design membership methods on `Clock` / `Design`, not root-visible helper files.

## User Confirmation Needed

The user confirmed that the listed true-new and renamed source names are acceptable. No additional rename is required from this review gate.

The originally reviewed uncertain terms were:

```text
Algorithm
Query
Collector
Data
Options
Result
Search
Problem
State
Catalog
Library
Draft
```
