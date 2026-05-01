# Research: industry CTS flow terminology

- Query: Research industry CTS flow terminology and architecture conventions from mature physical design tools, especially Cadence Innovus/CCOpt public material, and identify what should or should not be mirrored in an open-source CTS flow architecture.
- Scope: mixed
- Date: 2026-04-30

## Findings

### Files found

- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md`: task PRD; asks for a CTS flow architecture and naming redesign grounded in EDA industry CTS conventions and current code semantics.
- `.trellis/spec/backend/index.md`: backend spec index; points CTS flow refactors to directory structure, database, and quality guidelines.
- `.trellis/spec/backend/directory-structure.md`: defines the current iCTS flow framework as `read data -> synthesis/writeback -> evaluation -> report` and assigns flow subdirectory responsibilities.
- `.trellis/spec/backend/quality-guidelines.md`: defines CTS semantic naming expectations, including `clock tree`, `sink domain`, `source-to-root`, `downstream tree`, `root buffer`, `topology level`, `routing segment`, `flyline segment`, and `committed design object`.
- `.trellis/spec/backend/database-guidelines.md`: defines ownership and external-adapter boundaries, including that only synthesis/writeback commits CTS topology and evaluation/report/visualization stay readonly.
- `src/interface/tcl/tcl_icts/tcl_register_cts.h`: current user-facing CTS Tcl command registration.
- `src/interface/tcl/tcl_icts/tcl_cts.cpp`: current `run_cts`, `cts_report`, and `cts_save_tree` Tcl execution paths.
- `src/interface/tcl/tcl_icts/tcl_ctsconfig.cpp`: current user-facing CTS config option vocabulary.
- `src/interface/default_config/cts_default_config.json`: legacy/default JSON names for config knobs.
- `src/operation/iCTS/api/CTSAPI.cc`: external API boundary into run, init, report, reset, and summary output.
- `src/operation/iCTS/source/flow/FlowManager.hh`: flow manager public lifecycle methods and state.
- `src/operation/iCTS/source/flow/FlowManager.cc`: top-level CTS sequencing and key result reporting.
- `src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.cc`: read-data stage.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc`: synthesis stage, summary, and sink-domain status report.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc`: per-clock sink-domain synthesis coordinator.
- `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh`: sink-domain partition/context types.
- `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc`: sink-domain preparation and root-buffer/downstream-net construction.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc`: transaction boundary for rollback, sink-domain synthesis, source-to-root synthesis, and commit.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc`: writeback stage to iDB.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc`: evaluation stage.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc`: report/statistics/visualization stage.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh`: typed report/visualization view enums and data structures.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc`: string labels for CTS roles, phases, domains, and view modes.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc`: mapping from committed objects into typed clock-tree view nets, insts, routed segments, and flyline segments.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh`: synthesis facade for sink tree and source-to-root tree build results/options.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc`: dispatches sink-tree and source-to-root build logic.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh`: H-tree build options/results and topology-level reporting fields.
- `src/operation/iCTS/source/database/config/Config.hh`: active CTS config fields, defaults, and getters/setters.

### Code patterns

- Top-level local flow already mirrors a stage-oriented CTS lifecycle: `FlowManager::runCTS()` resets runtime metrics, starts a `CTS` stage, then calls `readData()`, `run()`, `writeback()`, and `evaluate()` before emitting key results (`src/operation/iCTS/source/flow/FlowManager.cc:62`, `src/operation/iCTS/source/flow/FlowManager.cc:68`).
- The current public/internal lifecycle verbs are short but ambiguous: `runCTS`, `readData`, `run`, `writeback`, `evaluate`, `report`, `outputRuntimeSetup`, `outputSummary`, and `reset` (`src/operation/iCTS/source/flow/FlowManager.hh:46`).
- Current Tcl surface registers `run_cts`, `cts_report`, `cts_save_tree`, and `cts_config` (`src/interface/tcl/tcl_icts/tcl_register_cts.h:34`). This already follows open-source-friendly verb-noun commands, but differs from OpenROAD's `clock_tree_synthesis` and `report_cts`.
- Current `run_cts` takes config/work-dir options and dispatches through the tool manager; `cts_report` can call tool-manager reporting or `CTS_API_INST.report(path)` (`src/interface/tcl/tcl_icts/tcl_cts.cpp:24`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:41`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:67`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:86`).
- Current config options expose physical/electrical CTS vocabulary such as `-skew_bound`, `-max_buf_tran`, `-max_sink_tran`, `-max_cap`, `-max_fanout`, `-routing_layer`, `-buffer_type`, `-root_buffer_type`, and `-level_skew_bound`, but also legacy/ambiguous names such as `-inherit_root`, `-break_long_wire`, `-shift_level`, and `-latency_opt_level` (`src/interface/tcl/tcl_icts/tcl_ctsconfig.cpp:27`).
- Runtime defaults contain older JSON vocabulary such as `router_type`, `delay_type`, `cluster_type`, `cluster_size`, `buffer_type`, and `use_netlist`; these are configuration terms rather than flow architecture names (`src/interface/default_config/cts_default_config.json:9`).
- The synthesis step reports `CTS Clock Tree Synthesis Summary` and `CTS Clock Tree Sink Domains`, using fields including `total_clocks`, `finished_clocks`, `skipped_clocks`, `failed_clocks`, `total_sink_domains`, `hard_macro_sinks`, and `regular_sinks` (`src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:41`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:55`).
- Per-clock synthesis partitions sinks into hard-macro and regular domains, prepares non-empty domains, synthesizes each sink domain, then synthesizes source-to-root connectivity (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:70`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:87`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:110`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:116`).
- Sink-domain preparation inserts a root buffer for a sink domain and creates a downstream net (`src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh:41`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh:55`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:69`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:76`).
- The transaction layer is a meaningful architecture boundary: it rolls back existing CTS membership, commits temporary inserted objects only after successful synthesis, records synthesis metrics, and rolls back on failure (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:119`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:160`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:165`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:203`).
- The local implementation already has a CTS-specific phase vocabulary: `read_data`, `downstream_htree`, `source_to_root_segment`, and `source_to_root_htree` (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:55`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc:156`).
- The local implementation also has role/domain vocabulary useful for reports: net roles `clock_source`, `source_to_root`, `downstream`, `sink_tree`; instance roles `clock_source`, `clock_load`, `root_buffer`, `htree_buffer`, `source_root_buffer`; domains `hard_macro`, `regular`, `source_to_root`; view modes `design` and `flyline` (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:35`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:44`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:64`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc:118`).
- Clock-tree view construction distinguishes routed tree segments from flyline/fallback segments, which matches industry debug/report expectations for visualizing inserted clock topology separately from ideal/fallback connectivity (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:39`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:96`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:263`).
- Writeback, evaluation, and reporting are separated after synthesis. Writeback records `CTS Writeback Summary`; evaluation records `CTSEvaluation`; report emits `CTS Report Mode`, statistics, SVG visualization, and GDS visualization (`src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:34`, `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc:31`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:75`).
- H-tree internals expose topology-depth, topology-level, characterization, slew/cap grids, inserted object counts, and root driver data; `H-tree` should remain a topology/algorithm term, not the blanket name for all CTS (`src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:57`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:73`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:101`).
- Backend specs already require CTS flow code to align with physical-design stages and preserve the sequence `read data -> synthesis/writeback -> evaluation -> report` (`.trellis/spec/backend/directory-structure.md:37`).
- Backend naming specs explicitly prefer CTS/physical-design terms and warn against generic backend/service names in CTS flow code (`.trellis/spec/backend/quality-guidelines.md:31`).
- Database specs say only synthesis/writeback may commit CTS-created topology; evaluation, report, and visualization must remain readonly (`.trellis/spec/backend/database-guidelines.md:58`).

### External references

- Cadence Innovus datasheet: Innovus includes a clock concurrent optimization engine that merges physical optimization with CTS, works from propagated clocks, accounts for clock gates/inter-clock paths/OCV derates, and includes a FlexH H-tree-like capability that optimizes insertion delay, power, and skew. Link: https://www.cadence.com/en_US/home/resources/datasheets/innovus-implementation-system-ds.html
- Cadence Innovus CCOpt training page: public course description lists generated constraints, clock properties, route types, CTS cells, stop/ignore pins, source latency, log/QoR analysis, worst-chain analysis, trial/cluster modes, and flexible H-tree implementation. It advertises versions 25.1, 23.1, and 22.1 and course release DDI251. Link: https://www.cadence.com/en_US/home/training/all-courses/86230.html
- Cadence blog on CTS/CCOpt: public Cadence blog says CCOpt merges timing optimization with CTS, leverages useful skew, and includes a visual debugger for violations; it also lists QoR/log/worst-chain analysis, route types, CTS cells, stop/ignore pins, source latency, trial and cluster modes, and flexible H-tree as learning topics. Link: https://community.cadence.com/cadence_blogs_8/b/di/posts/ccopt_5f00_clock_5f00_tree_5f00_synthesis
- Cadence Support public page, CCOpt clock spec generation: public teaser says CCOpt constraints for CTS are generated from SDC constraints and introduces clock trees and skew groups. Link: https://support1.cadence.com/public/docs/content/20453981.html
- Cadence Support public page, setting CCOpt properties: public teaser says CCOpt properties are used to meet clock targets during CTS. Link: https://support1.cadence.com/public/docs/content/20460894.html
- Cadence Support public page, Clock Tree Debugger: public teaser says CCOpt Clock Tree Debugger analyzes/debugs the inserted clock tree and supports cross-probing with the physical design window. Link: https://support1.cadence.com/public/docs/content/20502625.html
- mflowgen Innovus CTS node: describes the `cadence-innovus-cts` node as skew-balancing the clock tree, using a working Innovus checkpoint as input/output, `setup-ccopt.tcl` for clock tree optimization options, and GUI/Clock Tree Debugger review of gates, buffers, sinks, and insertion delay. Link: https://mflowgen.readthedocs.io/en/latest/stdlib-innovus-cts.html
- mflowgen Innovus Foundation Flow: documents major Innovus flow scripts as init, place, CTS, post-CTS hold fixing, route, postroute, and signoff, and recommends relying on Innovus Foundation Flow for canonical commands because Cadence changes recommended options over time. Link: https://mflowgen.readthedocs.io/en/latest/stdlib-innovus-flowsetup.html
- Cadence Community forum example: public user script shows the common command sequence `create_ccopt_clock_tree_spec -filename ccopt.spec`, `source ccopt.spec`, `ccopt_design -cts`. Use this only as evidence of common user vocabulary, not as an authoritative reference. Link: https://community.cadence.com/cadence_technology_forums/f/digital-implementation/45418/clock-tree-synthesis-error-using-innovus
- Public mirror of a 2015 Cadence CCOpt Concepts training deck: useful terminology evidence but not an official/current command reference. It distinguishes timing `Clock`, physical `Clock tree`, and balancing `Skew group`; describes `ccopt_design -cts`, `ccopt_design`, `optDesign -postCTS`, useful-skew controls, high/medium/low effort, trial CTS, clustering/DRV buffering, virtual delay balance, propagated clocks, implementation/routing, graph-based CTS, physical constraints, route types, buffer/inverter/clock-gating/delay cell properties, stop/ignore pins, insertion delay, and automatic spec creation from SDC. Link: https://studylib.net/doc/27687200/innovus-ccopt-concepts
- OpenROAD CTS documentation: open-source command vocabulary includes `configure_cts_characterization`, `clock_tree_synthesis`, `report_cts`, `set_cts_config`, `report_cts_config`, and `reset_cts_config`; options cover buffer lists, root/tree buffers, clock nets, sink and macro clustering, balance levels, NDR, obstruction awareness, repair clock nets, insertion-delay behavior, and report metrics such as clock roots, inserted buffers, clock subnets, and sinks. Link: https://openroad.readthedocs.io/en/latest/main/src/cts/README.html
- Synopsys Fusion Compiler datasheet: describes an integrated RTL-to-GDSII platform spanning design planning, placement, CTS, routing, physical optimization, chip finishing, signoff analysis, and ECO optimization, and highlights physically-aware synthesis and advanced CTS. Link: https://www.synopsys.com/implementation-and-signoff/resources/datasheets/fusion-compiler-ds.html
- Synopsys IC Compiler II page: public page positions IC Compiler II as a place-and-route solution with design planning, placement/optimization, CTS, routing convergence, manufacturing compliance, and signoff closure. Link: https://www.synopsys.com/implementation-and-signoff/physical-implementation/ic-compiler.html
- Synopsys Low-Power Flow User Guide public PDF mirror: CTS example uses `set_clock_tree_options`, `set_clock_tree_references`, and `clock_opt`; it describes bottom-up endpoint clustering by voltage area, subtree joining at the clock root, MMMC support, and the need for level shifters/isolation on clock nets crossing power domains. Link: https://picture.iczhiku.com/resource/eetop/wYIEdthEzTAOEVmc.pdf

### Common CTS phases and concepts

- Mature commercial flows do not treat CTS as only "buffer insertion." Cadence public material frames CTS as the transition from ideal-clock timing to propagated-clock timing, with buffering, sizing, placement, routing, skew/latency management, OCV/corner awareness, and post-CTS optimization effects. This supports keeping local CTS flow stages explicit instead of hiding them under one generic `run` module.
- Cadence/CCOpt public vocabulary separates `CTS` from `CCOpt`. `CTS` can mean clock-tree construction and global skew balancing; `CCOpt` means a broader concurrent clock/data optimization flow with useful skew and datapath optimization. An open-source CTS module should not call itself `ccopt` unless it truly implements concurrent clock/data optimization.
- A mature Cadence-style flow has user-observable phases around setup/spec generation, CTS/concurrent optimization, post-CTS optimization/hold fixing, routing, post-route optimization, and signoff. mflowgen exposes these as separate graph nodes: init, place, CTS, post-CTS hold, route, postroute, signoff.
- Cadence high-effort CCOpt terminology includes `trial CTS`, `cluster` or DRV buffering, `virtual delay balance`, switch to propagated clocks, global optimization, area reclaim, DRV optimization, useful-skew transforms, tree update, implementation, and clock net routing. These are useful as conceptual labels only when the local implementation has matching behavior.
- Cadence graph-based CTS terminology distinguishes physical buffering/clustering for DRV, constraint analysis, virtual delay balancing, and implementation using real buffers/wires. The local iCTS split into sink-domain preparation, sink-tree synthesis, source-to-root synthesis, commit, writeback, evaluation, and report is compatible with this style but is more algorithm-specific.
- Cadence terminology distinguishes three concepts that should remain separate in architecture and reports:
  - `Clock`: timing-analysis object derived from SDC.
  - `Clock tree`: physical graph/subset of circuitry CTS may operate on.
  - `Skew group`: balancing constraint object over a clock-tree graph.
- Local `sink domain` is not equivalent to Cadence `skew group`. Current iCTS uses `CTSSinkDomain` for hard-macro, regular, and source-to-root partitions. A skew group should only be introduced if iCTS models balancing constraints, multi-mode clocks, or per-mode sink sets.
- OpenROAD's terminology is directly relevant for open-source user-facing command style: `clock_tree_synthesis`, `configure_cts_characterization`, `report_cts`, `set_cts_config`, `report_cts_config`, and `reset_cts_config`. These names are clearer for users than proprietary `ccopt_*` names.
- OpenROAD options show common open-source CTS nouns: `buf_list`, `root_buf`, `tree_buf`, `clk_nets`, `wire_unit`, `distance_between_buffers`, `branching_point_buffers_distance`, `sink_clustering_*`, `macro_clustering_*`, `balance_levels`, `num_static_layers`, `obstruction_aware`, `apply_ndr`, `repair_clock_nets`, `no_insertion_delay`, `sink_buffer_max_cap_derate`, and `delay_buffer_derate`.
- Mature tools routinely separate register/regular sinks and macro sinks. OpenROAD exposes both sink and macro clustering options. Current iCTS has `hard_macro` and `regular` sink domains, which is a defensible open-source term pair.
- Mature CTS reports should surface clock roots, inserted buffers, clock subnets/nets, sinks, clock-tree structure, insertion delay/latency, skew, QoR, and debug visualization. Current iCTS already reports clocks, sink domains, inserted buffers/nets, wirelength, final buffer count/area, and visualization/report directories; future naming can align these with `clock_root_count`, `inserted_buffer_count`, `clock_subnet_count`, `sink_count`, `skew`, `insertion_delay`, and `clock_tree_wirelength`.

### User-facing command and option vocabulary

- Recommended user-facing command style for iCTS:
  - Prefer neutral open-source verbs: `run_cts`, `clock_tree_synthesis`, `configure_cts`, `report_cts`, `report_cts_config`, `reset_cts_config`, `write_cts_tree`.
  - Keep existing `run_cts`, `cts_report`, `cts_save_tree`, and `cts_config` only if compatibility requires them; consider aliases or migration docs if adopting OpenROAD-like names.
  - Avoid `ccopt_design`, `create_ccopt_clock_tree_spec`, `set_ccopt_property`, `setOptMode`, `optDesign`, `routeDesign`, `NanoRoute`, `FlexH`, and `Clock Tree Debugger` as iCTS command/module names. They are Cadence-specific or branded.
- Recommended option nouns to mirror:
  - Physical/electrical constraints: `max_transition` or `max_slew`, `max_capacitance` or `max_cap`, `max_fanout`, `max_wire_length`, `skew_bound` or `target_skew`, `insertion_delay` or `source_latency`, `wire_unit`, `routing_layers`, `apply_ndr`.
  - Cell sets: `buffer_cells`, `inverter_cells`, `clock_gating_cells`, `delay_cells`, `root_buffer`, `tree_buffer`, `sink_clustering_buffer`.
  - Clustering: `sink_clustering_enable`, `sink_clustering_size`, `sink_clustering_max_diameter`, `macro_clustering_size`, `macro_clustering_max_diameter`.
  - Clock selection and boundaries: `clock_nets`, `skip_nets`, `stop_pins`, `ignore_pins`, `sink_type`, `clock_source`, `clock_root`.
  - Reporting/output: `work_dir`, `log_file`, `statistics_dir`, `visualization_dir`, `out_file`, `save_dir`.
- Option names to revise or avoid in a new architecture:
  - Avoid `buf_tran` abbreviation in new user-facing names; prefer `max_buffer_transition` or `max_buffer_slew`.
  - Avoid `inherit_root`, `shift_level`, and `latency_opt_level` unless their behavior is documented in CTS terms; otherwise they read as internal heuristics.
  - Avoid using `level_*` for unrelated semantics. `topology_level_*`, `clock_tree_level_*`, or `clustering_level_*` are clearer.
  - Avoid `use_netlist` as a broad CTS behavior switch; prefer `clock_nets`/`netlist_clock_sources` if the behavior is selecting clocks/nets.

### Data and report naming conventions

- Recommended report table names:
  - `CTS Input Summary`
  - `CTS Synthesis Summary`
  - `CTS Clock Tree Summary`
  - `CTS Sink Domain Summary`
  - `CTS Writeback Summary`
  - `CTS Evaluation Summary`
  - `CTS Report Summary`
  - `CTS Runtime Summary`
- Recommended field names:
  - Counts: `clock_count`, `clock_root_count`, `sink_count`, `sink_domain_count`, `hard_macro_sink_count`, `regular_sink_count`, `inserted_buffer_count`, `inserted_net_count`, `clock_subnet_count`, `skipped_clock_count`, `failed_clock_count`.
  - Physical/timing: `target_skew`, `achieved_skew`, `insertion_delay`, `source_latency`, `max_transition`, `max_capacitance`, `max_fanout`, `clock_tree_wirelength`, `max_clock_net_wirelength`, `buffer_area`.
  - Topology: `clock_tree_depth`, `topology_level`, `topology_level_count`, `route_type`, `net_role`, `inst_role`, `synthesis_phase`, `sink_domain`, `routed_segment_count`, `flyline_segment_count`.
  - Status: `status`, `failure_reason`, `report_mode`, `evaluation_ready`, `writeback_done`, `design_ready`.
- Current iCTS already uses several good report labels and fields. The largest risk is inconsistent use of `htree` in summary names where future CTS might use non-H-tree topology. Keep `htree_*` fields only for H-tree-specific internals and expose generic `clock_tree_*` fields for user reports.

### What to mirror in open-source CTS architecture

- Mirror the broad physical-design lifecycle: setup/config, read/import clock data, build/synthesize clock tree, commit/writeback inserted objects, evaluate timing/physical QoR, report/visualize.
- Mirror the separation of timing clock, physical clock tree, and balancing group if and only if the implementation models all three. Current iCTS should keep `Clock`, `ClockTreeView`, and `SinkDomain` separate, and should reserve `SkewGroup` for a future balancing-constraint model.
- Mirror open-source command clarity from OpenROAD: `clock_tree_synthesis`, `configure_cts_characterization`, `report_cts`, and `set_cts_config` are useful conventions.
- Mirror common CTS nouns: clock source, clock root, sinks, macro sinks, regular/register sinks, root buffer, tree buffer, clock buffer cells, delay buffers, inserted buffers/nets, route type, NDR, trunk/leaf/top nets, insertion delay/latency, skew, slew/transition, capacitance, fanout, wirelength, sink clustering, macro clustering, obstruction awareness, repair clock nets.
- Mirror debug/report concepts: clock tree view, routed segments, flyline segments, clock-tree visualization, log/QoR summary, per-clock status, per-sink-domain status, failure reason.
- Mirror the idea that H-tree is one clock-tree topology option, not the name of the whole CTS flow. Local directories/types should keep `htree` only around the H-tree builder, characterization, topology search, and H-tree-specific metrics.

### What not to mirror directly

- Do not mirror Cadence trademarks or branded product architecture as local module names: `CCOpt`, `Clock Concurrent Optimization`, `FlexH`, `NanoRoute`, `GigaPlace`, and `Clock Tree Debugger`.
- Do not mirror Cadence command names as open-source API names unless building an explicit compatibility layer: `ccopt_design`, `create_ccopt_clock_tree_spec`, `create_ccopt_skew_group`, `set_ccopt_property`, `setOptMode`, and `optDesign` are vendor-specific.
- Do not call a local phase `useful_skew`, `post_cts_opt`, `trial_cts`, `cluster_mode`, or `global_opt` unless the implementation actually optimizes datapath slack or performs the named algorithmic phase.
- Do not use `skew_group` as a synonym for local `sink_domain`. Cadence-style skew groups are balancing constraints; current local sink domains are physical sink partitions.
- Do not expose generated spec-file editing as a first-class local workflow by copying Cadence's `ccopt.spec` pattern. For open source, prefer declarative config plus explicit APIs; generated debug artifacts can be reports, not mutable tool state.
- Do not overfit to a single vendor's option names. For open-source readability, use common EDA nouns and document aliases where needed.

### Architecture implications for the current redesign

- Keep `source/flow/stage/` for lifecycle orchestration, but consider more industry-readable class names:
  - `CTSClockDataLoadStep` -> `ClockDataImportStep` or `CTSInputLoadStep`
  - `CTSClockTreeSynthesisStep` -> `ClockTreeSynthesisStep`
  - `ClockTreeSynthesisDriver` -> `PerClockSynthesisDriver`
  - `ClockSinkDomainBuilder` -> `SinkDomainPreparation`
  - `ClockTreeSynthesisTransaction` -> `ClockTreeCommitTransaction`
  - `CTSClockTreeWritebackStep` -> `ClockTreeWritebackStep`
  - `CTSClockTreeEvaluationStep` -> `ClockTreeEvaluationStep`
  - `CTSClockTreeReportStep` -> `ClockTreeReportStep`
- Keep `source/flow/synthesis/` for algorithm orchestration over a net or tree, but avoid naming everything `ClockSynthesis` if it only builds a specific tree segment. Use `SinkTreeSynthesis`, `SourceToRootSynthesis`, and `ClockTreeSynthesis` for the aggregate boundary.
- Keep `source/flow/htree/` as algorithm-specific. If future topologies are added, create sibling topology modules rather than overloading `htree`.
- Keep `source/flow/clock_tree_view/` as the readonly report/debug model. This aligns with mature tool clock-tree debuggers without copying branded names.
- Keep `source/flow/evaluation/`, `source/flow/visualization/`, and `source/flow/netlist/` as readonly/reporting and netlist-editing boundaries, respectively, because this matches local specs and mature CTS flow boundaries.

### Related specs

- `.trellis/spec/backend/directory-structure.md`: authoritative placement rules for iCTS API/source/test layers, source categories, and CTS flow framework.
- `.trellis/spec/backend/quality-guidelines.md`: authoritative naming and CTS semantic boundary rules.
- `.trellis/spec/backend/database-guidelines.md`: authoritative ownership, adapter, writeback, and readonly evaluation/report rules.
- `.trellis/spec/frontend/index.md`: explicitly N/A; this is backend-only CTS work.

## Caveats / Not Found

- Official Cadence Innovus/CCOpt command reference manuals are not fully public. Public Cadence pages provide datasheets, blog/training descriptions, and support teasers, not complete syntax. Detailed command vocabulary above comes from a mix of public Cadence pages, mflowgen documentation, Cadence Community examples, and a public mirror of a 2015 Cadence training deck. Treat mirrored deck/forum examples as terminology evidence, not as an authoritative or current Innovus API contract.
- Public Cadence material emphasizes CCOpt as concurrent clock/data optimization. Current iCTS code does not appear to implement datapath useful-skew optimization, so `ccopt`/`concurrent optimization` naming should not be adopted for local architecture.
- This research inspected focused CTS flow, config, API, Tcl, and spec files. It did not exhaustively classify every file under `src/operation/iCTS/source/module/` or every test, because the user requested industry terminology and architecture conventions rather than full local code inventory.
- Source URLs may move behind vendor login over time. The artifact records the public URLs available on 2026-04-30.
