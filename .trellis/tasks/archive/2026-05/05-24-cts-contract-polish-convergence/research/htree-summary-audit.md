# Research: HTree summary audit

- Query: Audit current HTree/Topology/SourceTrunk summaries and tests for report-only diagnostic transport.
- Scope: internal
- Date: 2026-05-25

## Findings

### Files found

- `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh` - Declares `HTreeSummary`, `HTreeOutput`, `HTreeBuild`, and the large root-driver compensation report object.
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc` - Builds HTree, fills summary fields, emits the owning HTree report, and returns `HTree::Build`.
- `src/operation/iCTS/source/flow/synthesis/htree/characterization/Characterization.cc` - Writes characterization grid/cap/slew details into `HTreeSummary`.
- `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc` - Reads many HTree summary/report fields while emitting the HTree-owned synthesis report.
- `src/operation/iCTS/source/flow/synthesis/htree/solution/analytical/AnalyticalSolution.cc` - Writes analytical-selection counters into `HTreeSummary` and emits detailed diagnostics at the analytical HTree stage.
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc` - Copies selected root-driver compensation detail into `HTreeSummary`.
- `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh` - Declares `Topology::Summary` and `Topology::SourceTrunkSummary`; both embed full `HTree::Summary`.
- `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc` - Consumes `HTree::Build` for sink-domain topology and records it into `Topology::Build`.
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc` - Consumes `HTree::Build` for source-to-root HTree and records it into `SourceTrunkBuild`.
- `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc` - Moves the full `HTree::Summary` into topology/source-trunk summaries and derives narrow aggregation counters.
- `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc` - Aggregates topology/source-trunk summary counters into `SynthesisTraceSummary`.
- `src/operation/iCTS/source/flow/synthesis/trace/layout/ClockLayoutAdapter.cc` - Converts topology/source-trunk builds into committed layout topology and reads selected HTree depth.
- `src/operation/iCTS/test/flow/synthesis/TopologyRealTechSmokeTest.cc` - Topology smoke tests assert HTree diagnostics through `Topology::Summary::htree_summary`.
- `src/operation/iCTS/test/flow/synthesis/TopologyNonClusteredRealTechSmokeTest.cc` - Non-clustered topology tests assert the same transported HTree diagnostics.
- `src/operation/iCTS/test/flow/synthesis/TopologyRealTechMatrixRunner.cc` - Matrix tests record HTree diagnostic fields through `Topology::Summary::htree_summary`.
- `src/operation/iCTS/test/flow/synthesis/TopologyRealTechHTreeAssertions.cc` - Topology test helper takes `HTree::Summary` and checks frontier/load diagnostics.
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeBuildObservation.hh` - Existing HTree test-side observation helper already centralizes many direct `HTree::Build` diagnostic reads.

### Current contract shape

- `HTreeSummary` is broad. It starts as execution status (`success`, `failure_reason`) but also carries characterization setup, frontier counts, load-cap distributions, root-driver compensation detail, boundary-relaxation detail, analytical solver counters, log context, and object-name prefix at `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh:186`.
- `HTreeRootDriverCompensationReport` is detailed report material with physical load buckets, capacitance decomposition, clock period, slew buckets, delay, and power fields at `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh:135`.
- `Topology::Summary` embeds the entire `HTree::Summary` at `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh:158` and `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh:162`, then adds only a few topology-level aggregation fields.
- `Topology::SourceTrunkSummary` embeds the entire `HTree::Summary` at `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh:196` and `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh:200`, even though source-trunk production callers use only stage/status/count/depth-style data.

### Production consumption

- `SinkBranch.cc` uses `HTree::Build` only for immediate stage control before recording: `success`, `failure_reason`, `selected_depth`, inserted inst/net counts, and root-net identity at `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc:199`, `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc:200`, `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc:202`, `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc:210`, and `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc:219`.
- `SourceTrunk.cc` uses `HTree::Build` only for source-trunk stage control before recording: `success`, `failure_reason`, `selected_depth`, inserted inst/net counts, and `used_boundary_relaxation` at `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc:247`, `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc:248`, `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc:255`, and `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc:257`.
- `TopologyBuildTrace.cc` is the main broad transport point. It moves full `HTree::Summary` into `Topology::Summary` at `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc:109` and into `SourceTrunkSummary` at `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc:130`.
- The same trace code immediately derives the actual flow fields: selected level count, selected depth, inserted buffer count, inserted net count, and boundary-relaxation status at `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc:110`, `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc:111`, `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc:112`, `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc:113`, and `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc:131`.
- `recordClusterSinkNetLevels` reads nested `htree_summary.selected_depth` only to infer topology level for cluster sink nets at `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc:81` and `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc:91`.
- `Topology.cc` source-trunk aggregation reads only nested `htree_summary.selected_depth`, HTree output level count, and inserted counts at `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc:68`, `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc:70`, `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc:73`, `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc:74`, and `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc:75`.
- `ClockLayoutAdapter.cc` reads nested `SourceTrunkSummary::htree_summary.selected_depth` for source-to-root layout topology at `src/operation/iCTS/source/flow/synthesis/trace/layout/ClockLayoutAdapter.cc:123` and `src/operation/iCTS/source/flow/synthesis/trace/layout/ClockLayoutAdapter.cc:127`. The sink-domain path already uses narrow `Topology::Summary::selected_htree_depth` and `selected_htree_level_count` at `src/operation/iCTS/source/flow/synthesis/trace/layout/ClockLayoutAdapter.cc:91`, `src/operation/iCTS/source/flow/synthesis/trace/layout/ClockLayoutAdapter.cc:94`, and `src/operation/iCTS/source/flow/synthesis/trace/layout/ClockLayoutAdapter.cc:95`.
- HTree-owned production report emission legitimately reads detailed fields while still inside the HTree report stage: `SolutionReport.cc` reads `root_driver_compensation`, log context, analytical counters, frontier counts, load distribution, root-driver sizing, boundary-relaxation score, and selected root-driver cell at `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:150`, `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:192`, `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:203`, `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:210`, `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:247`, `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:309`, `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:320`, and `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:325`.

### Test consumption

- Topology tests currently depend on the full nested HTree summary for characterization grid facts and boundary inputs at `src/operation/iCTS/test/flow/synthesis/TopologyRealTechSmokeTest.cc:111`, `src/operation/iCTS/test/flow/synthesis/TopologyRealTechSmokeTest.cc:112`, `src/operation/iCTS/test/flow/synthesis/TopologyRealTechSmokeTest.cc:128`, `src/operation/iCTS/test/flow/synthesis/TopologyNonClusteredRealTechSmokeTest.cc:102`, and `src/operation/iCTS/test/flow/synthesis/TopologyNonClusteredRealTechSmokeTest.cc:114`.
- Topology test helpers assert HTree-internal behavior through `HTree::Summary`, including `force_branch_buffer`, `depth_candidate_count`, `selected_final_frontier_count`, and load-cap distribution at `src/operation/iCTS/test/flow/synthesis/TopologyRealTechHTreeAssertions.cc:86`, `src/operation/iCTS/test/flow/synthesis/TopologyRealTechHTreeAssertions.cc:93`, `src/operation/iCTS/test/flow/synthesis/TopologyRealTechHTreeAssertions.cc:105`, and `src/operation/iCTS/test/flow/synthesis/TopologyRealTechHTreeAssertions.cc:121`.
- Topology matrix tests persist HTree report/test fields from `result.summary.htree_summary` into experiment records: characterization grid fields, boundary-relaxation status, failure reason, final frontier count, and selected depth at `src/operation/iCTS/test/flow/synthesis/TopologyRealTechMatrixRunner.cc:460`, `src/operation/iCTS/test/flow/synthesis/TopologyRealTechMatrixRunner.cc:463`, `src/operation/iCTS/test/flow/synthesis/TopologyRealTechMatrixRunner.cc:464`, `src/operation/iCTS/test/flow/synthesis/TopologyRealTechMatrixRunner.cc:467`, and `src/operation/iCTS/test/flow/synthesis/TopologyRealTechMatrixRunner.cc:468`.
- HTree tests already have a test-side observation path: `ObserveHTreeBuild` extracts selected depth, frontier counts, load distribution, and boundary-relaxation fields from direct `HTree::Build` at `src/operation/iCTS/test/flow/synthesis/htree/HTreeBuildObservation.hh:60`.

### Field classification

Flow aggregation/control fields that are actually needed outside HTree:

- HTree direct caller control: `success`, `failure_reason`, and `selected_depth`. `used_boundary_relaxation` is useful as a narrow warning/status bit for caller stage summaries, but does not control algorithm behavior after HTree returns.
- Topology sink summary aggregation: `success`, `failure_reason`, `sink_clustering_enabled`, `selected_htree_level_count`, `selected_htree_depth`, `htree_inserted_buffer_count`, and `htree_inserted_net_count`.
- Topology source-trunk summary aggregation: `success`, `failure_reason`, `stage`, `selected_htree_depth`, inserted buffer/net counts, and `used_boundary_relaxation` for current stage summary reporting.
- Layout conversion: selected depth, selected level count, inserted inst/net level metadata from `Output`, and source-trunk/sink phase. Layout does not need characterization, frontier, root-driver, load-distribution, or analytical diagnostics.

Report-only or test-only fields in `HTreeSummary`:

- `char_wirelength_unit_um`, `char_wirelength_iterations`, `char_unique_level_bins`, `char_grid_adapted`, `char_max_slew_ns`, `char_max_cap_pf`, `char_slew_steps`, and `char_cap_steps`: assigned in HTree characterization at `src/operation/iCTS/source/flow/synthesis/htree/characterization/Characterization.cc:169`, but not needed by topology aggregation. `char_slew_steps` is also used internally during the same HTree build and should become a local build value rather than a returned summary dependency.
- `force_branch_buffer`, `root_driver_sizing_enabled`, `target_depth`, and `depth_explore_window`: configuration/report context, not downstream flow control.
- `depth_candidate_count`, `selected_final_frontier_count`, `selected_candidate_solution_count`, `selected_candidate_frontier_entry_count`, `selected_feasible_solution_count`, and `selected_feasible_frontier_entry_count`: report/test frontier diagnostics. `LogSynthesisSummary` already receives `selected_summary` and can keep using local selected-depth detail without exposing it through topology summaries.
- `min_top_input_slew_ns` and `top_input_slew_covering_idx`: boundary-report/test detail, not topology aggregation.
- `htree_load_group_count`, `htree_load_cap_min_pf`, `htree_load_cap_max_pf`, `htree_load_cap_mean_pf`, and `htree_load_cap_median_pf`: HTree report/load-distribution assertions. These are emitted from local `selected_summary` at `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:309`, so they do not need to travel through `Topology::Summary`.
- `selected_root_driver_cell_master` and `root_driver_compensation`: selected HTree report detail. `ApplyRootDriverCompensationSummary` copies a detailed local compensation result into the public summary at `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc:243`; this should be a report-detail object, not a production summary transport field.
- `boundary_relaxation_score` and `boundary_relaxation_reason`: report/test detail. Keep only a narrow `used_boundary_relaxation` status bit outside HTree unless a caller has a concrete warning aggregation need.
- `pruned_leaf_single_load_buffers`: HTree embedding/report detail.
- `analytical_mode_enabled`, `analytical_mode_selected`, `analytical_failure_reason`, and all `analytical_*` counts/ranges: HTree-owned analytical report detail. The analytical stage already emits detailed diagnostics directly at `src/operation/iCTS/source/flow/synthesis/htree/solution/analytical/AnalyticalSolution.cc:76`, `src/operation/iCTS/source/flow/synthesis/htree/solution/analytical/AnalyticalSolution.cc:123`, and `src/operation/iCTS/source/flow/synthesis/htree/solution/analytical/AnalyticalSolution.cc:249`.
- `log_context` and `object_name_prefix`: HTree report/embedding context. `Embedding.cc` currently reads `result.summary.object_name_prefix` during the same HTree build at `src/operation/iCTS/source/flow/synthesis/htree/embedding/Embedding.cc:511`; this should be local build context, not returned summary state.
- `failure_level` and `failure_length_idx`: declared in `HTreeSummary` at `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh:190`, but grep found no `result.summary.failure_level` or `result.summary.failure_length_idx` assignments. Similar fields exist on local topology-pruning results at `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.hh:57`, so the public summary fields look dead.

### Recommended minimal shape

Recommended public `HTreeSummary` shape:

```cpp
struct HTreeSummary
{
  bool success = false;
  std::string failure_reason;
  std::optional<unsigned> selected_depth = std::nullopt;
  bool used_boundary_relaxation = false;
};
```

If the HTree report still needs a structured object, create an HTree-owned report detail object or keep the relevant locals in the build function/report call. Do not embed that report detail in `Topology::Summary` or `SourceTrunkSummary`.

Recommended `Topology::Summary` shape:

```cpp
struct Summary
{
  bool success = false;
  std::string failure_reason;
  bool sink_clustering_enabled = false;
  std::optional<ClusterLeafDistanceSummary> cluster_leaf_distance_summary = std::nullopt;
  std::size_t selected_htree_level_count = 0U;
  std::optional<unsigned> selected_htree_depth = std::nullopt;
  std::size_t htree_inserted_buffer_count = 0U;
  std::size_t htree_inserted_net_count = 0U;
  bool used_boundary_relaxation = false;
};
```

`cluster_leaf_distance_summary` is not HTree transport and can be handled separately. If strict minimality is desired later, it can also be localized to the topology distance report stage.

Recommended `SourceTrunkSummary` shape:

```cpp
struct SourceTrunkSummary
{
  bool success = false;
  std::string failure_reason;
  SourceTrunkStage stage = SourceTrunkStage::kUnknown;
  std::optional<unsigned> selected_htree_depth = std::nullopt;
  std::size_t selected_htree_level_count = 0U;
  std::size_t inserted_buffer_count = 0U;
  std::size_t inserted_net_count = 0U;
  bool used_boundary_relaxation = false;
};
```

Remove both embedded `HTree::Summary htree_summary` members from `Topology.hh`.

### Test migration path

1. Migrate topology integration tests away from `result.summary.htree_summary`. Topology tests should assert topology integration behavior: `result.summary.success`, clustering status, root-net connectivity, inserted object ownership/counts, selected HTree depth/level count, layout conversion, and emitted report artifacts.
2. Move HTree algorithm-detail assertions from topology tests into HTree tests that call `HTree::build` directly. Use or extend `src/operation/iCTS/test/flow/synthesis/htree/HTreeBuildObservation.hh`.
3. For fields that are only needed to prove report content, assert against `cts.log` or detail report output. `TopologyRealTechArtifactAssertions.cc` already checks HTree load-distribution report strings in the log, which is the correct pattern for report-only data.
4. Convert `TopologyRealTechHTreeAssertions` helpers to consume either a narrow topology observation object or direct `HTreeBuildObservation`, not `Topology::Summary::htree_summary`.
5. For matrix/experiment tests, keep selected depth and failure reason from the narrow topology/source-trunk summaries. Characterization grid, frontier counts, load distributions, boundary scores, root-driver compensation, and analytical counters should come from HTree-specific matrix tests or report files.

### Grep checks

- Failed as a check, superseded by narrower greps: `rg -n "HTree|Topology|SourceTrunk|Summary|diagnostic|report" .trellis/spec src/operation/iCTS/source/flow src/operation/iCTS/source/module src/operation/iCTS/test 2>/dev/null`. This was too broad and mixed unrelated summaries/reports with the target transport path.
- Failed: `rg -n "htree_summary|HTreeSummary|SourceTrunkSummary|Topology::Summary|Summary summary|summary\\.htree|summary\\.[a-zA-Z0-9_]+" src/operation/iCTS/source/flow/synthesis src/operation/iCTS/test 2>/dev/null`. It found broad nested `htree_summary` transport plus many unrelated summary accesses.
- Failed: `rg -n "struct HTreeSummary|struct Summary|struct SourceTrunkSummary|htree_summary|diagnostic|selected|root_driver|attempt|relax|topology" src/operation/iCTS/source/flow/synthesis/htree src/operation/iCTS/source/flow/synthesis/topology src/operation/iCTS/source/flow/synthesis/trace src/operation/iCTS/test/flow/synthesis 2>/dev/null`. It confirmed the contract fields and broad transport/test dependence.
- Failed: `rg -n "build\\.summary\\.htree_summary|summary\\.htree_summary|htree_summary\\." src/operation/iCTS/source src/operation/iCTS/test 2>/dev/null`. It found production nested-summary reads and many topology tests consuming HTree diagnostics through topology summaries.
- Failed: `rg -n "result\\.summary\\.(char_|selected_|depth_|min_top|top_input|htree_load|root_driver|used_boundary|boundary_|pruned_|analytical_|force_branch|target_depth|root_driver_sizing|failure_|success|object_name|log_context)" src/operation/iCTS/source/flow/synthesis/htree src/operation/iCTS/source/flow/synthesis/topology 2>/dev/null`. It confirmed HTreeSummary is used as internal scratch/report state, not only returned status.
- Failed: `rg -n "HTree::Summary|HTreeSummary|RootDriverCompensationReport|root_driver_compensation|selected_final_frontier_count|selected_candidate_solution_count|selected_feasible_solution_count|boundary_relaxation_score|analytical_|char_wirelength|char_grid|htree_load_cap|htree_load_group|min_top_input_slew|top_input_slew_covering_idx|selected_root_driver_cell_master|pruned_leaf_single_load_buffers" src/operation/iCTS/source src/operation/iCTS/test 2>/dev/null`. It found report/test-only HTree details in production report code and tests.
- Failed: `rg -n "SourceTrunkSummary|source_trunk_build\\.summary|build\\.summary\\.(stage|inserted_buffer_count|inserted_net_count|used_boundary_relaxation)|summary\\.inserted_buffer_count|summary\\.inserted_net_count" src/operation/iCTS/source src/operation/iCTS/test 2>/dev/null`. It confirmed the source-trunk production surface uses narrow fields while the contract still embeds full HTree summary.
- Failed: `rg -n "Topology::Summary|synthesis_build\\.summary|Topology::Build|build\\.summary\\.(selected_htree|htree_inserted|sink_clustering|cluster_leaf|failure_reason|success)|summary\\.selected_htree|summary\\.htree_inserted|summary\\.cluster_leaf" src/operation/iCTS/source src/operation/iCTS/test 2>/dev/null`. It confirmed topology production aggregation uses narrow fields while the contract still embeds full HTree summary.
- Passed: `rg -n "LogSynthesisSummary|EmitDepthCandidateSummary|ApplyRootDriverCompensationSummary|selected_summary|DepthSearchSummary|DepthSummary|RootDriverCompensationStats|AnalyticalHTreeAttempt" src/operation/iCTS/source/flow/synthesis/htree src/operation/iCTS/test/flow/synthesis/htree 2>/dev/null`. This found local HTree report/detail structures that can carry report diagnostics without topology/source-trunk summary transport.
- Failed: `rg -n "failure_level|failure_length_idx|char_wirelength_unit_um|char_wirelength_iterations|char_unique_level_bins|char_grid_adapted|char_max_slew_ns|char_max_cap_pf|char_slew_steps|char_cap_steps|force_branch_buffer|root_driver_sizing_enabled|target_depth|depth_explore_window|selected_depth|depth_candidate_count|selected_final_frontier_count|selected_candidate_solution_count|selected_candidate_frontier_entry_count|selected_feasible_solution_count|selected_feasible_frontier_entry_count|min_top_input_slew_ns|top_input_slew_covering_idx|htree_load_group_count|htree_load_cap_min_pf|htree_load_cap_max_pf|htree_load_cap_mean_pf|htree_load_cap_median_pf|selected_root_driver_cell_master|root_driver_compensation|used_boundary_relaxation|boundary_relaxation_score|boundary_relaxation_reason|pruned_leaf_single_load_buffers|analytical_mode_enabled|analytical_mode_selected|analytical_failure_reason|analytical_model_set_count|analytical_rejected_fit_count|analytical_structural_cap_operator_count|analytical_evaluated_segment_count|analytical_generated_candidate_count|analytical_validated_candidate_count|analytical_validated_pareto_count|analytical_selected_pareto_power_rank|analytical_validated_delay_min_ns|analytical_validated_delay_median_ns|analytical_validated_delay_max_ns|analytical_validated_power_min_w|analytical_validated_power_median_w|analytical_validated_power_max_w|log_context|object_name_prefix" src/operation/iCTS/source src/operation/iCTS/test 2>/dev/null`. It found many HTreeSummary fields outside the contract, mostly in HTree-owned report/test paths.
- Passed: `rg -n "diagnostic_" src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh src/operation/iCTS/source/flow/synthesis/topology/Topology.hh src/operation/iCTS/source/flow/synthesis/htree src/operation/iCTS/test/flow/synthesis 2>/dev/null`. No `diagnostic_` fields are present in `HTreeSummary`, `Topology::Summary`, or `SourceTrunkSummary`; analytical diagnostics remain in analytical solver/report/test paths.
- Failed: `rg -n "HTree::Summary htree_summary|HTreeSummary htree_summary|htree_summary" src/operation/iCTS/source/flow/synthesis/topology/Topology.hh src/operation/iCTS/source/flow/synthesis src/operation/iCTS/test/flow/synthesis 2>/dev/null`. It found the two broad embedded summaries plus production move/read sites and topology tests.
- Failed: `rg -n "summary\\.failure_level|summary\\.failure_length_idx|failure_level =|failure_length_idx =" src/operation/iCTS/source/flow/synthesis/htree src/operation/iCTS/test/flow/synthesis/htree 2>/dev/null`. It found local topology-pruning fields but no writes/reads of `HTreeSummary::failure_level` or `HTreeSummary::failure_length_idx`.
- Failed: `rg -n "result\\.summary\\.htree_summary\\.(char_|selected_final|selected_candidate|selected_feasible|htree_load|min_top|top_input|root_driver|boundary_|analytical_|force_branch|root_driver_sizing|pruned|log_context|object_name)" src/operation/iCTS/source src/operation/iCTS/test 2>/dev/null`. It found topology tests consuming report-only HTree fields through the topology summary.
- Passed with test caveat: `rg -n "result\\.summary\\.htree_summary\\.(success|failure_reason|selected_depth|used_boundary_relaxation)" src/operation/iCTS/source src/operation/iCTS/test 2>/dev/null`. Production only needs selected depth from the nested source-trunk summary; topology tests also use status/failure/boundary fields.
- Failed: `rg -n "build\\.summary\\.htree_summary = std::move|SourceTrunkSummary|struct Summary|HTree::Summary htree_summary" src/operation/iCTS/source/flow/synthesis/topology src/operation/iCTS/source/flow/synthesis/trace/topology_build 2>/dev/null`. It found the exact broad move/copy sites and embedded summary declarations to remove.

## External References

- None. This is an internal code/worktree audit; no external API or version-specific documentation was needed.

## Related Specs

- `.trellis/tasks/05-24-cts-contract-polish-convergence/prd.md` - Requires slim HTree/topology/source-trunk summaries and no broad report-only HTree diagnostics across module boundaries.
- `.trellis/tasks/05-24-cts-contract-polish-convergence/design.md` - Defines contract taxonomy and says detailed HTree diagnostics should be owned by HTree build/report code.
- `.trellis/tasks/05-24-cts-contract-polish-convergence/implement.md` - Lists the planned HTree/topology/source-trunk summary slimming and grep validation.
- `.trellis/spec/backend/quality-guidelines.md` - Allows module-qualified flow contracts but forbids broad snapshots without a clear stage contract.
- `.trellis/spec/backend/database-guidelines.md` - Says report-only data should be narrow and typed.
- `.trellis/spec/backend/logging-guidelines.md` - Places structured report output in schema/report helpers.

## Caveats / Not Found

- `python3 ./.trellis/scripts/task.py current --source` reported no active task for this shell, so the user-provided task directory `.trellis/tasks/05-24-cts-contract-polish-convergence` was used as the output root.
- No source code was modified and no build/tests were run; this is a read-only audit plus this research file.
- Line references are from the current worktree on 2026-05-25.
- I found no production consumer that needs characterization grid details, frontier counts, root-driver compensation detail, load-cap distribution, boundary-relaxation score/reason, analytical counters, or log context after `HTree::build` returns to topology/source-trunk callers.
- I found no assignments to `HTreeSummary::failure_level` or `HTreeSummary::failure_length_idx`; matching failure-location fields appear to be local topology-pruning details instead.
