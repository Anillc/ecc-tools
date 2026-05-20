# CTS Source Rename Table

## Naming Rule

New names must state a CTS object, an owned action, or a boundary-specific data contract. Avoid generic engineering words such as
Internal, Support, Request, Response, Snapshot, Types, Input, Session, rollback, fallback, and Network.

## Confirmed Source Domains

| Domain | Old name | New name | CTS meaning |
| --- | --- | --- | --- |
| iDB writeback | WrapperClockWriterInternal.hh | ClockIdbWritebackData.hh | iDB clock writeback restore scope and pin membership |
| iDB writeback | WrapperClockWriterSupport.cc | ClockIdbPinMembership.cc | iDB net pin membership mutation routines |
| QoR evaluation | QorEvaluationInternal.hh | ClockQorMetricCollector.hh | Clock-net QoR roles, measurements, and collector contracts |
| H-tree analytical solver | AnalyticalSolverInternal.hh | AnalyticalHTreeCandidateSearch.hh | Analytical H-tree candidate scoring/search contracts |
| H-tree analytical solver | AnalyticalSolverRequest.cc | AnalyticalSolveProblem.cc | Analytical H-tree solve-problem validation and diagnostics |
| H-tree analytical solver | AnalyticalSolverRequest | AnalyticalHTreeSolveProblem | Full candidate-search problem definition |
| Clock layout trace | ClockLayoutSynthesisInput.hh | ClockLayoutSynthesisTopology.hh | Synthesis topology metadata used by clock layout projection |
| Root-driver compensation | RootDriverCompensationInternal.hh | RootDriverCompensationState.hh | Root-driver compensation state, cache keys, and root-load estimates |
| BST routing | BSTTypes.hh | BSTRoutingConfig.hh | Bounded-skew routing topology mode, RC pattern, and parameters |
| BST routing | BSTRouterInternal.hh | BstClockTreeConversion.hh | Conversion between BST area trees and CTS clock route trees |
| Fast clustering | FastClusteringInternal.hh | FastClusteringDraft.hh | Sink-load cluster drafts, bounds, neighbor graph, and polish moves |
| Cluster constraints | ClusterConstraintTypes.hh | ClusterConstraintEvaluation.hh | Cluster legality and electrical evaluation result data |
| Clock design data | ClockNetwork.hh/.cc | ClockTreeDesignRecord.hh/.cc | Clock-tree role records over design-owned objects |
| STA adapter | STAAdapterInternal.hh/.cc | STAAdapterTimingQuery.hh/.cc | STA timing, Liberty, iDB timing-context, and wire-RC query helpers |
| SDC adapter | ClockTraceResolverInternal.hh | SdcClockTraceAlgorithm.hh | SDC clock trace traversal, ownership, and report helpers |
| FastSTA aggregate | FastStaTypes.hh | FastStaClockAnalysisData.hh | FastSTA public clock analysis data aggregate |
| FastSTA DMP solver | FastStaDmpCeffInternal.hh | FastStaDmpCeffSolver.hh | DMP effective-capacitance solver state and equation declarations |
| Optimization model | OptimizationTypes.hh | ClockSizingOptimizationData.hh | Clock sizing optimization state, trials, mutations, and metrics |

## Deferred Test Cleanup

Test helper file/type naming remains out of this child task unless a source-facing public rename requires test references to compile.
