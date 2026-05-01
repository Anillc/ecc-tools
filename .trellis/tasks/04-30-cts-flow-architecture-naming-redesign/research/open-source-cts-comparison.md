# Research: open-source CTS comparison

- Query: Research open-source or publicly documented CTS implementations and terminology, especially OpenROAD/TritonCTS and academic/industrial CTS literature, then compare phase names, module boundaries, and naming conventions against this repo's iCTS flow.
- Scope: mixed
- Date: 2026-04-30

## Findings

### Files found

- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md`: task PRD; requests a CTS flow architecture and naming redesign grounded in physical-design CTS terminology and current code semantics.
- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/industry-cts-flow-terminology.md`: related industry terminology research; useful for Cadence/Innovus/CCOpt terminology caveats.
- `.trellis/spec/backend/index.md`: backend spec index; says CTS flow refactors should consult directory structure, database, and quality guidelines.
- `.trellis/spec/backend/directory-structure.md`: defines local iCTS source categories and the CTS flow framework `read data -> synthesis/writeback -> evaluation -> report`.
- `.trellis/spec/backend/quality-guidelines.md`: related CTS naming rules, including clock-tree, sink-domain, source-to-root, downstream tree, topology level, routed/flyline segments, and committed design object terminology.
- `.trellis/spec/backend/database-guidelines.md`: related ownership boundary rules; only synthesis/writeback may commit CTS-created topology, while evaluation/report/visualization are readonly.
- `src/operation/iCTS/api/CTSAPI.hh`: external iCTS API surface for run/report/init/reset/summary.
- `src/operation/iCTS/api/CTSAPI.cc`: API implementation dispatches to `FlowManager` and run setup.
- `src/operation/iCTS/source/flow/CMakeLists.txt`: local flow module list and CMake target boundaries.
- `src/operation/iCTS/source/flow/FlowManager.hh`: top-level local CTS lifecycle API and state.
- `src/operation/iCTS/source/flow/FlowManager.cc`: top-level sequence for `runCTS`, `readData`, `run`, `writeback`, `evaluate`, and `report`.
- `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.*`: local run setup for config/workdir/logging/adapter initialization.
- `src/operation/iCTS/source/flow/stage/*`: local stage/orchestration boundary for data load, synthesis, writeback, evaluation, report, per-clock driver, sink-domain preparation, status, and transaction/rollback.
- `src/operation/iCTS/source/flow/synthesis/*`: local clock-tree synthesis facade and sink-tree/source-to-root dispatch helpers.
- `src/operation/iCTS/source/flow/htree/*`: local H-tree topology, characterization, depth search, level planning, and object-build internals.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.*`: local clock-net mutation helper used by read-data, sink-domain preparation, rollback, and commit.
- `src/operation/iCTS/source/flow/clock_tree_view/*`: local readonly clock-tree view model shared by report and visualization.
- `src/operation/iCTS/source/flow/evaluation/*`: local evaluation/statistics boundary.
- `src/operation/iCTS/source/flow/visualization/*`: local SVG/GDS visualization boundary.

### Code patterns

- Local top-level iCTS sequence is stage-oriented but uses one ambiguous verb: `FlowManager::runCTS()` calls `readData()`, `run()`, `writeback()`, and `evaluate()` in order, then `report()` is invoked separately (`src/operation/iCTS/source/flow/FlowManager.cc:62`, `src/operation/iCTS/source/flow/FlowManager.cc:68`, `src/operation/iCTS/source/flow/FlowManager.cc:120`).
- The public `FlowManager` lifecycle names are `runCTS`, `readData`, `run`, `evaluate`, `report`, plus private `writeback`; `run()` is the least descriptive because it actually runs synthesis (`src/operation/iCTS/source/flow/FlowManager.hh:46`, `src/operation/iCTS/source/flow/FlowManager.hh:65`, `src/operation/iCTS/source/flow/FlowManager.cc:97`, `src/operation/iCTS/source/flow/FlowManager.cc:102`).
- Local flow CMake already exposes distinct conceptual modules: `clock_tree_view`, `evaluation`, `htree`, `netlist`, `visualization`, `run_setup`, `stage`, and `synthesis` (`src/operation/iCTS/source/flow/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/CMakeLists.txt:10`, `src/operation/iCTS/source/flow/CMakeLists.txt:19`).
- Local synthesis stage reports `CTS Clock Tree Synthesis Summary` and `CTS Clock Tree Sink Domains`; fields distinguish total/finished/skipped/failed clocks, sink domains, hard macro sinks, and regular sinks (`src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:41`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:55`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:63`).
- Local per-clock synthesis partitions sinks into hard-macro and regular domains, prepares each non-empty domain, synthesizes sink domains, then synthesizes the source-to-root segment or H-tree (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:70`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:87`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:108`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:116`).
- Local transaction code is a real architectural boundary: it rolls back prior clock CTS membership, builds pending views, commits inserted objects after success, records metrics, and rolls back on failure (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:119`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:160`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:165`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:203`).
- Local `ClockSynthesis` facade dispatches separate sink-tree and source-to-root build flows, and source-to-root has explicit `kSegment` and `kHTree` stages (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:31`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:41`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:62`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:47`).
- Local sink-tree synthesis performs load collection, optional sink clustering, H-tree option construction, H-tree build, and metrics/reporting (`src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:37`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:57`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:66`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:77`).
- Local source-to-root synthesis dispatches to a top segment when there is one root input and to H-tree when there are multiple root inputs (`src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:37`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:62`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:76`).
- Local H-tree builder owns H-tree-specific concerns: topology generation, characterization, build option resolution, level plans, depth candidate search, best-characterization selection, root-driver sizing, and inserted object construction (`src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:45`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:57`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:101`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:131`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:143`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:173`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:281`).
- Local readonly view model exposes CTS report/debug vocabulary: net roles `clock_source`, `source_to_root`, `downstream`, `sink_tree`; inst roles `logic_cell`, `clock_source`, `clock_load`, `root_buffer`, `htree_buffer`, `source_root_buffer`; synthesis phases `read_data`, `downstream_htree`, `source_to_root_segment`, `source_to_root_htree`; sink domains `hard_macro`, `regular`, `source_to_root` (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:35`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:44`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:55`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:64`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc:118`).

### External references

- OpenROAD CTS documentation, latest public docs, accessed 2026-04-30: OpenROAD's CTS module is based on TritonCTS 2.0 and is invoked with `clock_tree_synthesis`; docs also expose `configure_cts_characterization`, `report_cts`, `set_cts_config`, `report_cts_config`, and `reset_cts_config`. Link: https://openroad.readthedocs.io/en/latest/main/src/cts/README.html
- OpenROAD CTS documentation, command/options evidence: `clock_tree_synthesis` options include `buf_list`, `root_buf`, `clk_nets`, `tree_buf`, `sink_clustering_*`, `macro_clustering_*`, `balance_levels`, `obstruction_aware`, `apply_ndr`, `insertion_delay`, `repair_clock_nets`, and `no_insertion_delay`; `report_cts` reports clock roots, buffers inserted, clock subnets, and sinks. Link: https://openroad.readthedocs.io/en/latest/main/src/cts/README.html
- OpenROAD source, `TritonCTS::runTritonCts`: current main branch sequence is `setupCharacterization`, `findClockRoots`, `populateTritonCTS`, `checkCharacterization`, `buildClockTrees`, `writeDataToDb`, `setAllClocksPropagated`, optional `repairClockNets`, and `balanceMacroRegisterLatencies`. Link: https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/src/cts/src/TritonCTS.cpp#L85-L105
- OpenROAD source, `setupCharacterization`: selects root/sink buffers, derives fanout/wirelength constraints, creates `TechChar`, and resets CTS metrics. Link: https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/src/cts/src/TritonCTS.cpp#L198-L255
- OpenROAD source, clock-root/sink discovery: `populateTritonCTS()` finds clock nets from user input or OpenSTA/SDC, clones clock gaters, initializes clock trees, and records clock roots. Link: https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/src/cts/src/TritonCTS.cpp#L1202-L1287
- OpenROAD source, macro/register partitioning: `initClockTreeForMacrosAndRegs()` separates macro sinks and register sinks, may create a second net for register sinks, and assigns `MacroTree`/`RegisterTree` tree types. Link: https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/src/cts/src/TritonCTS.cpp#L1367-L1447
- OpenROAD source, `HTreeBuilder::preSinkClustering`: sink clustering is an H-tree builder concern, with separate macro/register cluster knobs and automatic cluster-size/diameter exploration. Link: https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/src/cts/src/HTreeBuilder.cpp#L38-L175
- OpenROAD source, `HTreeBuilder::run`: the H-tree builder logs H-tree topology generation, handles sink clustering and sink-region initialization, explores levels until stop criteria, optionally legalizes, then creates clock subnets. Link: https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/src/cts/src/HTreeBuilder.cpp#L1232-L1332
- OpenROAD source, CTS module boundary: `cts_lib` contains `Clock`, `TreeBuilder`, `HTreeBuilder`, `SinkClustering`, `TechChar`, `TritonCTS`, `Clustering`, `LatencyBalancer`, and `CtsOptions`; SWIG/Tcl and optional Python bindings expose the module. Link: https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/src/cts/src/CMakeLists.txt#L9-L25
- iCTS public paper: Li et al., "iCTS: Iterative and Hierarchical Clock Tree Synthesis with Skew-Latency-Load Tree", IEEE TCAD 2025, DOI 10.1109/TCAD.2025.3549355. The abstract describes an iterative hierarchical CTS framework composed of clustering, topology generation and routing, buffering, and optimization; it also states iCTS is integrated into iEDA. Link: https://www.cse.cuhk.edu.hk/~byu/papers/J143-TCAD2025-iCTS.pdf
- Generalized H-tree literature: Kahng, Li, and Wang, "Optimal Generalized H-Tree Topology and Buffering for High-Performance and Low-Power Clock Distribution", IEEE TCAD 2018. The paper treats H-tree/GH-tree as a topology family and co-optimizes topology and buffering under skew/latency/power tradeoffs. Link: https://vlsicad.ucsd.edu/Publications/Journals/j128.pdf
- Bounded-skew/DME literature: Kahng and Robins, "A New Class of Iterative Steiner Tree Heuristics with Good Performance" / bounded-skew routing work as represented in the UCSD VLSICAD public paper; the BST/DME terminology frames clock trees around source/sinks, topology, embedding, skew bound, wirelength/cost, and bottom-up/top-down construction. Link: https://vlsicad.ucsd.edu/Publications/Journals/j32_pub.pdf
- Industry flow reference, mflowgen Innovus CTS node: public documentation names the Innovus CTS step as skew-balancing the clock tree and discusses setup files, Clock Tree Debugger review, gates, buffers, sinks, and insertion delay. Link: https://mflowgen.readthedocs.io/en/latest/stdlib-innovus-cts.html
- Industry flow reference, mflowgen Innovus Foundation Flow: public documentation positions CTS between placement and post-CTS hold fixing/routing, reinforcing that "CTS" is a flow stage, while routing/signoff/postroute are separate stages. Link: https://mflowgen.readthedocs.io/en/latest/stdlib-innovus-flowsetup.html

### Comparison: phase names

OpenROAD/TritonCTS uses a user-facing phase name of `clock_tree_synthesis`, plus explicit configuration/reporting commands. Its internal flow reads as `setupCharacterization -> findClockRoots -> populateTritonCTS -> checkCharacterization -> buildClockTrees -> writeDataToDb -> setAllClocksPropagated -> repairClockNets -> balanceMacroRegisterLatencies` (OpenROAD `TritonCTS.cpp` links above).

The local iCTS top-level phase sequence is simpler and cleaner for this repo's current behavior: `readData -> run/synthesis -> writeback -> evaluate -> report`. The main mismatch is not the sequence but the name `run()`: compared with OpenROAD's public `clock_tree_synthesis` and internal `buildClockTrees`, local `run()` hides the fact that this is the synthesis/build phase.

Academic iCTS terminology names the algorithmic pipeline as clustering, topology generation/routing, buffering, and optimization. Local code contains clustering, H-tree topology/characterization/object build, source-to-root synthesis, and commit/writeback. It does not yet expose a distinct general optimization stage comparable to the paper's final optimization phase or OpenROAD's optional repair/latency balancing stage. Naming a local directory `optimization` would be premature unless such behavior is implemented.

Recommended phase-name mapping for design discussion:

| External/public term | Current local term | Research note |
|---|---|---|
| `clock_tree_synthesis` / `buildClockTrees` | `FlowManager::run()` and `CTSClockTreeSynthesisStep` | Prefer `synthesizeClockTrees()` or `runSynthesis()` over generic `run()`. |
| `configure_cts_characterization` / setup characterization | `run_setup`, H-tree `CharacterizationLibrary`, `HTreeCharacterizationFlow` | Keep setup and characterization separate; setup is run-level, characterization is algorithm-level. |
| clock-root discovery / clock-net population | `CTSClockDataLoadStep`, `ClockNetEditor::readClockData()` | `loadClockData` or `importClockData` is clearer than broad `readData`. |
| macro/register tree split | `CTSSinkDomain::kHardMacro`, `kRegular` | Local `hard_macro`/`regular` is defensible; do not call it `skew_group` unless balancing constraints are modeled. |
| H-tree topology generation | `flow/htree/HTreeBuilder` | Keep `htree` algorithm-specific; do not use H-tree as the whole CTS flow name. |
| write clock data to DB/OpenDB | `CTSClockTreeWritebackStep` | `writeback` aligns with local DB abstraction; `writeDataToDb` is OpenROAD/OpenDB-specific. |
| repair clock nets / latency balancing | not a distinct local stage | Do not create `repair` or `latency_balance` stage names until implemented. |
| report CTS / Clock Tree Debugger | `evaluation`, `report`, `visualization`, `clock_tree_view` | Local readonly `clock_tree_view` is a good neutral debug/report model name. |

### Comparison: module boundaries

OpenROAD has a compact CTS module: `TritonCTS` coordinates the overall flow, `CtsOptions` owns configuration, `TechChar` owns on-the-fly characterization, `TreeBuilder`/`HTreeBuilder` build clock trees, `SinkClustering`/`Clustering` handle clustering, `LatencyBalancer` handles post-build latency balancing, and DB/OpenSTA/Resizer/Steiner dependencies are direct module collaborators.

Local iCTS has more explicit subdirectories under `source/flow`: setup, orchestration stages, netlist mutation, synthesis facade, H-tree internals, readonly clock-tree view, evaluation, and visualization. This is more modular than OpenROAD and matches local specs. The cost is naming friction: `stage`, `synthesis`, and `htree` overlap unless their responsibilities are explained.

The cleanest local responsibility boundaries, based on the code, are:

- `run_setup`: run initialization only; config, work directory, logging, wrapper/adapter setup.
- `stage`: lifecycle orchestration; no algorithm internals beyond sequencing, per-clock/domain coordination, transaction boundaries, status reporting.
- `netlist`: design/clock-net mutation primitives used by data load, preparation, rollback, and commit.
- `synthesis`: aggregate tree-building facade over a prepared net: sink-tree synthesis, source-to-root synthesis, optional clustering, H-tree option construction, synthesis metrics.
- `htree`: H-tree-specific topology/characterization/depth search/object construction; should remain topology-specific.
- `clock_tree_view`: readonly report/debug model, not an algorithm or DB owner.
- `evaluation`: readonly evaluation/statistics over committed CTS results.
- `visualization`: readonly SVG/GDS writers over `ClockTreeView`.

This boundary differs from OpenROAD, where H-tree builder owns sink clustering, sink-region initialization, legalization, and subnet creation. Local iCTS splits those across `stage`, `synthesis`, `htree`, `netlist`, and `clock_tree_view`, which is acceptable if names stay precise.

### Comparison: naming conventions

OpenROAD's user-facing names are clear and generic: `clock_tree_synthesis`, `report_cts`, `set_cts_config`, `reset_cts_config`, `buf_list`, `root_buf`, `tree_buf`, `clk_nets`, `sink_clustering_*`, `macro_clustering_*`, `balance_levels`, `obstruction_aware`, `apply_ndr`, `insertion_delay`, `repair_clock_nets`. Its report nouns are also useful: clock roots, buffers inserted, clock subnets, and sinks.

Academic/public iCTS literature uses algorithm names and metrics: clustering, topology generation/routing, buffering, optimization, skew-latency-load tree, skew, latency, load capacitance, clock capacitance, clock routing topology, insertion buffer count, buffer area, clock wirelength, and final evaluation with commercial/OpenROAD baselines.

Local iCTS names already have good domain specificity in `ClockTreeView`: `source_to_root`, `downstream`, `sink_tree`, `root_buffer`, `htree_buffer`, `source_root_buffer`, `hard_macro`, `regular`, `read_data`, `downstream_htree`, `source_to_root_segment`, and `source_to_root_htree`. These are more precise than copying OpenROAD's generic `TreeBuilder`/`SubNet` names.

Naming risks in local code:

- `FlowManager::run()` is too generic for the synthesis phase.
- `stage` is a broad directory name; it is acceptable as lifecycle orchestration, but file/class names should carry domain meaning.
- `ClockSynthesis` can read like the whole CTS flow, but its code is really a facade for sink-tree and source-to-root tree build operations.
- `htree_*` summary fields should remain H-tree-specific; user-facing summaries should prefer generic `clock_tree_*` names when the value is not inherently H-tree-only.
- `source_to_root` is a useful local term, but it is not a standard OpenROAD command concept; keep it as internal/report terminology for the segment from clock source to sink-domain root buffers.
- `hard_macro` vs `regular` maps better to local behavior than OpenROAD's `macro`/`register` split if regular sinks may include more than registers.

### Architecture and naming implications

- Keep the top-level user concept as `CTS` / `clock tree synthesis`, not `H-tree`. H-tree is an algorithm/topology module.
- Use `clock_tree_synthesis` as the canonical phase vocabulary for the main build phase. Existing user API names such as `run_cts` can remain compatibility aliases, but internal code should not hide synthesis behind `run`.
- Preserve `sink_domain` as a local concept. Do not rename it to `skew_group`; public industrial material uses skew groups for balancing constraints, whereas current iCTS sink domains are physical/semantic partitions.
- Keep `source_to_root` and `downstream` as internal/report names because they describe the current two-part construction: source-to-domain-root and domain-root-to-sinks.
- Treat `ClockTreeView` as a strong local abstraction. It is the neutral equivalent of public "clock tree debugger" output without adopting vendor-branded terminology.
- Consider naming the per-clock driver boundary around what it coordinates, e.g. `PerClockSynthesisDriver` or `ClockTreeSynthesisCoordinator`, rather than mirroring OpenROAD's generic `TreeBuilder`.
- Consider naming the transaction boundary explicitly around commit/rollback, e.g. `ClockTreeSynthesisTransaction` is acceptable, while `ClockTreeCommitTransaction` would make the persistence responsibility clearer.
- Avoid adding `repair`, `latency_balance`, `useful_skew`, `post_cts_opt`, or `ccopt` modules until behavior exists. These are meaningful public/industrial concepts but would overstate the current implementation.

### Related specs

- `.trellis/spec/backend/directory-structure.md`: authoritative placement and flow framework rules for iCTS; currently names the CTS framework as `read data -> synthesis/writeback -> evaluation -> report`.
- `.trellis/spec/backend/quality-guidelines.md`: authoritative local naming and semantic-boundary expectations.
- `.trellis/spec/backend/database-guidelines.md`: authoritative rules for database ownership, wrapper/STAAdapter boundaries, and readonly evaluation/report behavior.
- `.trellis/spec/guides/cross-layer-thinking-guide.md`: relevant if the redesign later changes APIs, flow outputs, database writeback, report schema, or external adapter boundaries.
- `.trellis/spec/guides/code-reuse-thinking-guide.md`: relevant if the redesign creates helper modules or moves CMake targets.
- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/industry-cts-flow-terminology.md`: related research with deeper Innovus/CCOpt terminology notes and caveats.

## Caveats / Not Found

- Official commercial CTS/CCOpt implementation internals are not fully public. For industrial terminology this artifact relies on public mflowgen/Cadence-facing descriptions and the sibling `industry-cts-flow-terminology.md` research artifact; it does not claim access to proprietary Innovus/ICC2 internals.
- OpenROAD source links point to the `master` branch as accessed on 2026-04-30; exact line numbers can drift if upstream changes. The concepts cited are stable enough for terminology comparison, but a future implementation task should pin a commit if exact source-line reproducibility matters.
- OpenROAD/TritonCTS is not a perfect architectural template for this repo. OpenROAD centralizes more behavior in `TritonCTS`/`HTreeBuilder`, while this repo has intentionally separated stage orchestration, synthesis facade, H-tree internals, netlist mutation, view, evaluation, and visualization.
- The public iCTS TCAD paper describes a richer iterative hierarchical CTS algorithm with clustering, topology/routing, buffering, and optimization. The local code inspected here has matching concepts in parts, but this artifact did not verify that every paper-level algorithmic phase is implemented under `source/flow`.
- No write was made to the old duplicate-date task path during this corrected write. The artifact path is the corrected task directory requested by the user.
