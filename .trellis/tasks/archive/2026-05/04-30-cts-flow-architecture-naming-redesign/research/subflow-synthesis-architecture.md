# Research: subflow synthesis architecture

- Query: Research the proposed `synthesis` subflow for task `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign`. Inspect current `src/operation/iCTS/source/flow/stage`, `synthesis`, `htree`, and clock-tree algorithm call chain. Focus on algorithm-body responsibilities and propose readable second-level architectural subfolders that are not overly direct implementation labels like `sink_domain` or `per_clock`. Consider topology/H-tree, clock planning, domain preparation, tree construction, candidate/characterization internals, and module-owned data managers.
- Scope: internal
- Date: 2026-04-30

## Findings

### Files found

- `src/operation/iCTS/api/CTSAPI.cc` - external API delegates `runCTS()` and `report()` into `FlowManager`.
- `src/operation/iCTS/source/flow/FlowManager.cc` - top-level lifecycle calls `readData()`, `run()`, `writeback()`, and `evaluate()`.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc` - synthesis-stage clock loop, summary aggregation, and stage status.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc` - per-clock coordinator for rollback, sink-domain preparation, downstream synthesis, source-to-root synthesis, and view merge.
- `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh` - current public sink-domain preparation structs.
- `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc` - current sink classification, root-buffer insertion, and downstream-net setup.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc` - transaction boundary for rollback, sink-domain synthesis commit, source-to-root synthesis commit, and run-summary updates.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh` - current algorithm facade with build options/results for downstream and source-to-root synthesis.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc` - thin dispatcher into sink-tree and source-to-root internal builders.
- `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc` - downstream sink-tree build assembly: validate root net, prepare loads, run H-tree, record result.
- `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc` - source-to-root dispatch between single segment and top H-tree.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc` - optional clustering and cluster-buffer temporary object preparation.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisHtreeOptions.cc` - resolves H-tree and top-segment options from synthesis/runtime constraints.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.hh` - synthesis-local temporary net/object helper and side-effect guards.
- `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc` - transfers temporary H-tree/segment objects and selected topology metrics into synthesis results.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh` - H-tree public facade, options, level plans, inserted-object metadata, and large build result.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` - end-to-end H-tree algorithm body: topology, characterization, planning, candidate search, selection, object build, logging.
- `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh` - internal function surface for characterization, level planning, depth search, candidate assembly, filtering, selection, and construction.
- `src/operation/iCTS/source/flow/htree/CharacterizationLibrary.hh` - reusable characterization cache/data manager shared by synthesis stages.
- `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc` - characterization grid planning, cache ensure, and H-tree characterization summary.
- `src/operation/iCTS/source/flow/htree/HTreeLevelPlan.cc` - level-plan derivation, characterization length indices, and depth-candidate resolution.
- `src/operation/iCTS/source/flow/htree/HTreeSegmentCandidateFrontier.cc` - required segment frontier synthesis from characterized segment patterns.
- `src/operation/iCTS/source/flow/htree/HTreeTopologyDepthSearch.cc` - depth-candidate exploration loop and global candidate pools.
- `src/operation/iCTS/source/flow/htree/HTreeTopologyAssembly.cc` - candidate topology assembly, boundary filtering, sink-load filtering, and selection.
- `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc` - temporary object construction for selected H-tree patterns.
- `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.hh` - public source-to-root single-segment builder.
- `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.cc` - source-to-root segment characterization, selection, and temporary object construction.
- `src/operation/iCTS/source/module/topology/TopologyGen.hh` - reusable topology generation API used by H-tree synthesis and sink clustering.
- `src/operation/iCTS/source/module/characterization/CharBuilder.hh` - segment characterization owner used through `CharacterizationLibrary`.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.hh` - current final and temporary netlist mutation facade.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc` - final commit of algorithm-owned temporary objects into `Design` and `Clock`.
- `src/operation/iCTS/source/database/design/Design.hh` - final design ownership of clocks, instances, pins, and nets.
- `src/operation/iCTS/source/database/design/Clock.hh` - clock identity, source/load anchors, and final CTS membership view.

### Current call chain

- Public execution enters at `CTSAPI::runCTS()` and delegates directly to `FLOW_MANAGER_INST.runCTS()` (`src/operation/iCTS/api/CTSAPI.cc:67`, `src/operation/iCTS/api/CTSAPI.cc:69`).
- `FlowManager::runCTS()` calls `readData()`, `run()`, `writeback()`, and `evaluate()` in order (`src/operation/iCTS/source/flow/FlowManager.cc:62`, `src/operation/iCTS/source/flow/FlowManager.cc:68`).
- `FlowManager::run()` delegates the algorithm stage to `CTSClockTreeSynthesisStep::run(_clock_tree_view)` (`src/operation/iCTS/source/flow/FlowManager.cc:97`, `src/operation/iCTS/source/flow/FlowManager.cc:102`).
- `CTSClockTreeSynthesisStep::run()` resets the view, reads all clocks from `DESIGN_INST`, loops over each clock, and calls `ClockTreeSynthesisDriver::run(...)` (`src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:63`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:69`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:72`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:82`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:90`).
- `ClockTreeSynthesisDriver::run()` rolls back the clock, resolves clock source/net, partitions sinks, appends sink metadata to a per-clock view, creates one shared `CharacterizationLibrary`, prepares sink-domain contexts, synthesizes downstream domains, synthesizes source-to-root, then merges the per-clock view into the global view (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:44`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:49`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:70`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:79`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:82`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:87`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:110`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:116`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:119`).
- Downstream synthesis calls `ClockSynthesis::build(...)`; source-to-root synthesis calls `ClockSynthesis::buildSourceToRoot(...)` (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:188`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:196`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:236`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:241`).
- `ClockSynthesis` is only a dispatcher today: downstream build calls `clock_synthesis::BuildSinkTree(...)`, and source-to-root calls `clock_synthesis::BuildSourceToRootTree(...)` (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:36`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:38`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:41`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:44`).
- `BuildSinkTree()` validates root driver/loads, guards root-net side effects, prepares direct or clustered H-tree sinks, builds H-tree options, runs `HTreeBuilder::build`, records H-tree results, and optionally emits clustering distance tables (`src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:37`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:40`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:47`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:54`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:57`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:66`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:67`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:77`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:85`).
- `BuildSourceToRootTree()` validates source/root inputs, guards source-net side effects, reconnects the source net, dispatches one root input to `SourceToRootSegmentBuilder`, and dispatches multiple root inputs to `HTreeBuilder` (`src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:37`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:41`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:47`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:60`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:62`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:65`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:76`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:78`).
- `HTreeBuilder::build()` performs topology generation, characterization, option resolution, level planning, depth candidate resolution, segment frontier synthesis, topology-depth search, sink-load profile filtering, best candidate selection, best pattern materialization, root-driver sizing validation/application, temporary object construction, and summary logging (`src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:81`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:104`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:127`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:138`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:143`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:150`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:164`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:173`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:178`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:185`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:252`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:274`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:281`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:291`).

### Algorithm-body responsibility boundaries

- `stage/` currently contains algorithm-body decisions, not just stage sequencing. `ClockTreeSynthesisDriver` is a clock-level synthesis planner and coordinator, while `ClockSinkDomainBuilder` prepares algorithm inputs and root/downstream objects (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:70`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:82`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:87`; `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:47`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:55`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:69`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:76`).
- `stage/ClockTreeSynthesisTransaction` is mixed: it has genuine algorithm control (`synthesizeSinkDomain`, `synthesizeSourceToRoot`), status/report data transfer, rollback, and final object commit (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:160`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:178`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:216`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:254`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:257`).
- `synthesis/` currently owns temporary algorithm objects and side effects. `ClockSynthesis::BuildResult` exposes H-tree result internals, cluster metadata, inserted object ownership, and topology levels (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:89`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:95`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:96`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:104`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:107`).
- `synthesis/ClockSynthesisSinkClustering` is more than a clustering adapter: it resolves runtime electrical constraints, calls topology clustering, creates cluster buffers/nets, and returns H-tree sinks (`src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:126`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:136`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:151`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:180`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:193`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:205`).
- `synthesis/ClockSynthesisNetEditor` owns temporary side-effect rollback and object creation helpers, which are algorithm-local and distinct from final `ClockNetEditor::commitInsertedObjects` (`src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.hh:41`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.hh:47`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.hh:52`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.hh:73`; `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:444`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:502`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:530`).
- `htree/` is a full algorithm pipeline, not just an H-tree entry class. Its public `BuildResult` contains topology, plan, characterization, search, profile, fallback, temporary object, and root state in one aggregate (`src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:101`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:107`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:109`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:123`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:132`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:145`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:151`).
- The H-tree internals already expose natural responsibility seams through `HTreeBuilderInternal`: characterization/grid, level plan/depth, depth search/evaluation, candidate assembly/filtering, sink-load legality, selection, root-driver sizing, and object building (`src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:50`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:56`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:64`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:66`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:69`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:90`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:94`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:99`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:110`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:113`, `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh:115`).
- Final design ownership remains in `Design`, while `Clock` keeps borrowed membership views (`src/operation/iCTS/source/database/design/Design.hh:66`, `src/operation/iCTS/source/database/design/Design.hh:69`, `src/operation/iCTS/source/database/design/Design.hh:72`, `src/operation/iCTS/source/database/design/Design.hh:77`, `src/operation/iCTS/source/database/design/Design.hh:93`; `src/operation/iCTS/source/database/design/Clock.hh:42`, `src/operation/iCTS/source/database/design/Clock.hh:57`, `src/operation/iCTS/source/database/design/Clock.hh:62`). This supports keeping final materialization in the proposed `instantiation` layer, while `synthesis/construction` owns only temporary algorithm products.

### Recommended second-level `synthesis` architecture

Recommended balanced layout:

```text
src/operation/iCTS/source/flow/synthesis/
  Synthesis.hh
  Synthesis.cc

  planning/
    Planning.hh
    Planning.cc

  preparation/
    Preparation.hh
    Preparation.cc

  topology/
    Topology.hh
    Topology.cc
    htree/
      HTree.hh
      HTree.cc

  characterization/
    Characterization.hh
    Characterization.cc

  exploration/
    Exploration.hh
    Exploration.cc

  construction/
    Construction.hh
    Construction.cc

  diagnostics/
    Diagnostics.hh
    Diagnostics.cc
```

Why these names:

- `planning/` is the readable home for clock-level synthesis intent: per-clock plan assembly, source/root validation, source-to-root lengths, run policy, and the coarse sequence of domains and topology stages. It avoids the implementation label `per_clock` while still describing the algorithm's decision-making role.
- `preparation/` is the readable home for input preparation before topology search: load classification, domain readiness, root/downstream preparation, and clustering input preparation. It avoids the narrow folder name `sink_domain`; domain concepts can remain type names under this folder.
- `topology/` is the public home for topology-family dispatch and H-tree entry points. It can contain `topology/htree/` if the H-tree code remains large, but external callers should enter through `topology/Topology.hh` or `topology/htree/HTree.hh`, not individual search files.
- `characterization/` is the home for characterization grid planning, cache/reuse, CharBuilder setup, and boundary lattice adaptation. This name is a CTS/EDA concept rather than a helper label, and it fits current `CharacterizationLibrary` and `HTreeCharacterizationFlow`.
- `exploration/` is a better architectural name than `candidate/` for frontier generation, depth search, candidate pools, sink-load legality filtering, and final selection. Candidate structs can remain internal types under this folder, but the folder describes the algorithm phase.
- `construction/` is the home for turning selected algorithm patterns into temporary CTS objects and result batches. It must be named carefully in design notes as "temporary synthesis construction", not final design/iDB instantiation.
- `diagnostics/` is the home for algorithm-owned summaries, metrics, and debug/report tables produced during synthesis. This prevents `report/` from absorbing algorithm internals and prevents logging files from sitting beside entry files.

### Suggested current-to-proposed mapping

| Current responsibility | Current files | Proposed home |
|---|---|---|
| Top-level synthesis entry and result contract | `synthesis/ClockSynthesis.*`, algorithm parts of `stage/CTSClockTreeSynthesisStep.cc` | `synthesis/Synthesis.*` |
| Clock-level algorithm plan and sequence | `stage/ClockTreeSynthesisDriver.*`, algorithm parts of `stage/ClockTreeSynthesisTransaction.*` | `synthesis/planning/` |
| Domain/load preparation | `stage/ClockSinkDomainBuilder.*`, preparation parts of `synthesis/ClockSynthesisSinkClustering.*` | `synthesis/preparation/` |
| H-tree/top-segment topology family entry | `htree/HTreeBuilder.*`, `htree/SourceToRootSegmentBuilder.*`, `synthesis/ClockSynthesisHtreeOptions.*` | `synthesis/topology/` and `synthesis/topology/htree/` |
| Characterization cache and grid adaptation | `htree/CharacterizationLibrary.*`, `htree/HTreeCharacterizationFlow.cc`, `htree/HTreeCharacterizationTypes.hh`, `module/characterization/CharBuilder*` adapter usage | `synthesis/characterization/` |
| Candidate/frontier/depth search and selection | `htree/HTreeCandidateTypes.hh`, `htree/HTreePatternRegistry.hh`, `htree/HTreeSegmentCandidateFrontier.cc`, `htree/HTreeTopologyDepthSearch.cc`, `htree/HTreeTopologyDepthEvaluation.cc`, `htree/HTreeTopologyAssembly.cc`, `htree/HTreeSinkLoadProfile*` | `synthesis/exploration/` |
| Temporary algorithm object construction and side effects | `synthesis/ClockSynthesisNetEditor.*`, `htree/HTreeClockTreeObjectBuilder.cc`, `htree/HTreeClockTopologyBuildContext.hh`, object-transfer parts of `synthesis/ClockTreeSynthesisMetrics.cc` | `synthesis/construction/` |
| Synthesis metrics and algorithm status output | `synthesis/ClockTreeSynthesisMetrics.*`, `synthesis/ClockSynthesisReporter.*`, `htree/HTreeSynthesisSummary.cc`, `htree/HTreeLogging.cc`, status-table emission currently in `stage/` | `synthesis/diagnostics/` |
| Final commit to `Design`/`Clock` and iDB writeback | `netlist/ClockNetEditor::commitInsertedObjects`, `stage/CTSClockTreeWritebackStep.*` | Not `synthesis`; belongs to proposed `instantiation/` or the existing netlist/writeback boundary |

### H-tree internal decomposition

If `topology/htree/` remains more than one entry pair, use the same architectural names under it instead of a flat helper list:

```text
synthesis/topology/htree/
  HTree.hh
  HTree.cc
  characterization/
  exploration/
  construction/
  diagnostics/
```

Recommended placement:

- `topology/htree/characterization/`: `CharacterizationLibrary`, characterization grid plan, H-tree characterization flow, and cache types.
- `topology/htree/exploration/`: level plans, segment frontiers, topology pattern registry, depth search/evaluation, sink-load profile legality, and selection.
- `topology/htree/construction/`: temporary object builder, naming/build context, root-driver sizing application.
- `topology/htree/diagnostics/`: H-tree synthesis summaries and H-tree algorithm logging.

This nested version is useful only if keeping all H-tree internals under `topology/htree/` is preferable for local cohesion. If the project wants all second-level folders under `synthesis/` to be peer concepts, keep `characterization/`, `exploration/`, `construction/`, and `diagnostics/` directly under `synthesis/` and have the H-tree entry use them as internal modules.

### Module-owned data managers

- Avoid a second-level folder named `data/`, `types/`, `context/`, or `managers/`. Those names tend to collect unrelated structs and recreate the current readability issue.
- Put module-owned managers next to the responsibility they manage:
  - `planning/ClockSynthesisPlan` or `planning/SynthesisPlan` for clock/domain/source-to-root decisions.
  - `preparation/PreparationContext` or `preparation/DomainPlan` for prepared domain inputs. The type name may still mention domain; the folder should not be the overly direct `sink_domain`.
  - `characterization/CharacterizationLibrary` or `characterization/CharacterizationStore` for reusable `CharBuilder` state.
  - `exploration/CandidateSpace`, `exploration/PatternRepository`, or `exploration/DepthSearch` for frontiers, candidate pools, and pattern registries.
  - `construction/ConstructionBatch` or `construction/TemporaryTreeObjects` for temporary owned `Inst`/`Pin`/`Net` products before final instantiation.
- Keep fine-grained candidate keys, frontier structs, and legality cache structs private to the folder or an `internal/` subdirectory when they are not stable module contracts. Current examples such as `CandidateBuildEvaluation`, `HTreeTopologyDepthSearchResult`, and `TopologyPatternRegistry` are algorithm internals, not cross-flow contracts (`src/operation/iCTS/source/flow/htree/HTreeCandidateTypes.hh:39`, `src/operation/iCTS/source/flow/htree/HTreeCandidateTypes.hh:102`, `src/operation/iCTS/source/flow/htree/HTreePatternRegistry.hh:180`).

### Recommendation

Use this second-level architecture for the proposed `synthesis` subflow:

```text
synthesis/
  Synthesis.hh
  Synthesis.cc
  planning/
  preparation/
  topology/
  characterization/
  exploration/
  construction/
  diagnostics/
```

This is more readable than `sink_domain/`, `per_clock/`, or `candidate/` because each folder describes an algorithm phase or architectural responsibility rather than a loop axis, data partition, or implementation record. It also keeps the high-level `synthesis` layer cohesive: plan the clock tree, prepare inputs, generate/select topology, characterize/evaluate candidates, construct temporary results, and emit algorithm diagnostics. Final design/iDB materialization should remain outside `synthesis` under the proposed `instantiation` layer.

## External References

- No new external web references were consulted for this artifact. The naming proposal is grounded in current local code inspection plus existing task research files:
  - `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/industry-cts-flow-terminology.md`
  - `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/open-source-cts-comparison.md`
  - `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/current-cts-flow-code-map.md`

## Related Specs

- `.trellis/spec/backend/directory-structure.md` - source categories, flow placement, current CTS flow boundary expectations, and CMake target structure.
- `.trellis/spec/backend/quality-guidelines.md` - CTS semantic naming and avoidance of broad snapshots or behavior by string matching.
- `.trellis/spec/backend/database-guidelines.md` - final ownership in `Design`, borrowed clock membership views, external adapter boundaries, and readonly evaluation/report boundaries.

## Caveats / Not Found

- This artifact does not modify source code and does not validate CMake moves.
- The proposed split is architectural. A later implementation plan should decide whether H-tree internals live as peers under `synthesis/` or nested under `synthesis/topology/htree/`.
- Current code commits temporary synthesis objects into `Design` during the synthesis transaction. The proposed architecture should eventually move final materialization into `instantiation`, but that migration is broader than a folder rename.
- Current `ClockTreeViewAdapter` and view-building calls blur synthesis/report-model boundaries. The proposed structure treats them as temporary result projection until the planned `database/design/ClockTree` or equivalent stable model boundary is decided.
