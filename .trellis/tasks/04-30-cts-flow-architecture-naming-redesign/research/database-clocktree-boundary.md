# Research: database/design ClockTree boundary

- Query: Research `database/design/ClockTree` responsibility for task `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign`; inspect current `database/design/Clock.hh`, `Design.hh/cc`, `flow/clock_tree_view/*`, report/visualization consumers, evaluation consumers, and synthesis producers. Define exactly what stable CTS clock-tree design data belongs in `database/design/ClockTree`, what must remain in synthesis/instantiation/evaluation/report, and how to avoid turning it into a bag of tiny structs.
- Scope: internal
- Date: 2026-04-30

## Findings

### Summary Decision

`database/design/ClockTree` should be the stable CTS semantic tree over `Design`-owned objects. It should explain, per `Clock`, which committed CTS objects participate in source-to-root and sink-domain distribution, what semantic roles those objects have, which sink domain they belong to, and the coarse selected topology levels needed by downstream consumers.

It must not own final `Inst`, `Pin`, or `Net` objects, must not create or commit objects, must not write iDB, must not carry H-tree search/candidate state, must not store evaluation metrics, and must not store report/visualization formatting. The effective owner/lifetime should be `Design`/`Clock` lifecycle: one `ClockTree` per `Clock`, reset with design topology rollback, containing borrowed pointers or stable object names only.

The shortest boundary rule is:

```text
Design owns objects; Clock owns clock identity and membership view; ClockTree owns stable CTS roles/domains/topology semantics over those objects; synthesis/instantiation/evaluation/report own their process-specific state.
```

### Files Found

- `src/operation/iCTS/source/database/design/Clock.hh` - one CTS clock's input identity and borrowed final membership view.
- `src/operation/iCTS/source/database/design/Design.hh` - singleton database owner for `Clock`, `Inst`, `Pin`, and `Net`.
- `src/operation/iCTS/source/database/design/Design.cc` - commit, lookup, reset, removal, and clock distribution helpers.
- `src/operation/iCTS/source/database/design/CMakeLists.txt` - current design DB target contains only `Design.cc`, with public design include path.
- `src/operation/iCTS/source/flow/FlowManager.hh` - flow manager currently owns `ClockTreeView`, evaluation state, run summary, and writeback state.
- `src/operation/iCTS/source/flow/FlowManager.cc` - lifecycle order: read data, synthesis, writeback, evaluation, report.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh` - current readonly report/visualization view with CTS roles, sink domains, synthesis phases, segment geometry, and status flags.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc` - view reset, status flags, append/lookups, and enum string conversions.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewSynthesisInput.hh` - narrow synthesis-to-view input structs.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.hh` - builder facade for sink-domain/source-to-root view records.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc` - derives routed/flyline segments from nets, routing trees, and synthesis topology level metadata.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.hh` - normalized visualization model records.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc` - report-only model builder, including Design/Wrapper fallbacks and logic cell geometry.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.hh` - per-clock synthesis commit/rollback boundary.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc` - builds pending view records, commits inserted objects, records run summary, and rolls back on failure.
- `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh` - sink-domain preparation context with root buffer/downstream net anchors.
- `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc` - partitions sinks, creates root buffers, and creates downstream nets.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.hh` - final clock-netlist mutation and commit facade.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc` - reads clocks, partitions sinks, creates root buffers/nets, reconnects nets, and commits temporary objects into `Design`.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh` - synthesis API, options, result structs, and temporary object ownership.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc` - delegates sink-tree and source-to-root synthesis.
- `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc` - downstream sink-tree synthesis coordination.
- `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc` - source-to-root segment/HTree dispatch.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.hh` - temporary object creation and side-effect guards.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc` - creates temporary buffers/nets and restores root/source net side effects.
- `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.hh` - synthesis metrics and temporary ownership transfer declarations.
- `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc` - absorbs temporary H-tree/segment-owned objects into synthesis results.
- `src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.hh` - adapter from synthesis results to view input records.
- `src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.cc` - maps inserted inst/net levels into view input records before commit.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh` - H-tree build options, level plans, inserted object level metadata, and build result.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` - H-tree topology generation, characterization, candidate search, selection, object build, and summary logging.
- `src/operation/iCTS/source/flow/htree/HTreeCandidateTypes.hh` - depth search/candidate/evaluation structs.
- `src/operation/iCTS/source/flow/htree/HTreeCharacterizationTypes.hh` - characterization grid and resolved build option structs.
- `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc` - builds temporary CTS inst/pin/net objects for selected H-tree topology.
- `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.hh` - source-to-root segment build options/results and temporary ownership.
- `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.cc` - source-to-root segment characterization, selection, and temporary object creation.
- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh` - evaluation summary, options, state, and facade.
- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc` - reads committed design clocks/nets/insts, updates STA/RC, computes wirelength/area/timing, and writes statistics.
- `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh` - statistics data and report writer facade.
- `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc` - writes statistics report files and log tables.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc` - evaluation stage orchestration.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc` - report orchestration, statistics output, SVG/GDS visualization output.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc` - writes committed CTS clocks back to iDB through `Wrapper`.
- `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc` - consumes `ClockTreeView` through visualization model and writes SVG reports.
- `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc` - consumes `ClockTreeView` through visualization model and writes GDS/LYP reports.
- `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh` - report-only semantic layer key/palette data.
- `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.cc` - maps visualization roles/levels to GDS display layers.
- `src/operation/iCTS/api/CTSAPI.hh` - public CTS API facade.
- `src/operation/iCTS/api/CTSAPI.cc` - adapts evaluation summary into feature summary.
- `src/feature/database/feature_icts.h` - external feature summary shape.
- `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp` - platform CTS run/report entry points plus legacy GUI file/tree helpers.
- `src/interface/gui/interface/icts_io.cpp` - GUI consumes CTS tree node maps from tool/platform layer.
- `src/interface/gui/src/mainwindow_cts.cpp` - GUI menu action for CTS.

### Code Patterns

#### Current Database Ownership

- `Clock` stores clock identity plus borrowed source/source-net/load/inst/net pointers, not ownership (`src/operation/iCTS/source/database/design/Clock.hh:43`, `src/operation/iCTS/source/database/design/Clock.hh:47`, `src/operation/iCTS/source/database/design/Clock.hh:48`, `src/operation/iCTS/source/database/design/Clock.hh:49`).
- `Clock` membership helpers only append borrowed pointers and clear membership vectors (`src/operation/iCTS/source/database/design/Clock.hh:57`, `src/operation/iCTS/source/database/design/Clock.hh:62`).
- `Design` owns objects with `std::unique_ptr` vectors and keeps lookup indexes for insts, pins, and nets (`src/operation/iCTS/source/database/design/Design.hh:93`, `src/operation/iCTS/source/database/design/Design.hh:94`, `src/operation/iCTS/source/database/design/Design.hh:95`, `src/operation/iCTS/source/database/design/Design.hh:96`, `src/operation/iCTS/source/database/design/Design.hh:97`, `src/operation/iCTS/source/database/design/Design.hh:98`, `src/operation/iCTS/source/database/design/Design.hh:99`).
- `Design::get_*` returns borrowed pointer vectors collected from the owned vectors (`src/operation/iCTS/source/database/design/Design.cc:46`, `src/operation/iCTS/source/database/design/Design.cc:107`, `src/operation/iCTS/source/database/design/Design.cc:112`, `src/operation/iCTS/source/database/design/Design.cc:117`, `src/operation/iCTS/source/database/design/Design.cc:122`).
- `Design::commitInst`, `commitPin`, and `commitNet` move temporary ownership into final design storage, update topology edges/indexes, and reject duplicate names (`src/operation/iCTS/source/database/design/Design.cc:184`, `src/operation/iCTS/source/database/design/Design.cc:190`, `src/operation/iCTS/source/database/design/Design.cc:259`, `src/operation/iCTS/source/database/design/Design.cc:265`, `src/operation/iCTS/source/database/design/Design.cc:343`, `src/operation/iCTS/source/database/design/Design.cc:349`).
- `Design::removeClockMembershipObjects` removes clock-member nets except the clock source net, then removes member insts (`src/operation/iCTS/source/database/design/Design.cc:438`, `src/operation/iCTS/source/database/design/Design.cc:440`, `src/operation/iCTS/source/database/design/Design.cc:448`).

Implication for `ClockTree`: it must borrow `Clock`/`Inst`/`Pin`/`Net` pointers or store stable object names, but never own those objects. It must be reset/rolled back with `Design` topology so borrowed pointers do not survive owner reset.

#### Current Flow-Local Clock-Tree View

- `FlowManager` currently owns `_clock_tree_view`, `_evaluation_state`, `_writeback_result`, and `_evaluation_ready` as flow runtime state (`src/operation/iCTS/source/flow/FlowManager.hh:68`, `src/operation/iCTS/source/flow/FlowManager.hh:69`, `src/operation/iCTS/source/flow/FlowManager.hh:70`, `src/operation/iCTS/source/flow/FlowManager.hh:71`, `src/operation/iCTS/source/flow/FlowManager.hh:73`).
- The flow lifecycle resets view/evaluation/writeback state on read/run, then runs synthesis, writeback, evaluation, and report in sequence (`src/operation/iCTS/source/flow/FlowManager.cc:87`, `src/operation/iCTS/source/flow/FlowManager.cc:97`, `src/operation/iCTS/source/flow/FlowManager.cc:105`, `src/operation/iCTS/source/flow/FlowManager.cc:115`, `src/operation/iCTS/source/flow/FlowManager.cc:120`).
- `ClockTreeView` currently mixes stable-ish semantic roles (`CTSNetRole`, `CTSInstRole`, `CTSSinkDomain`) with report-specific segment geometry and flow status flags (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:35`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:44`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:64`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:79`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:95`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:110`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:126`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:142`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:144`).
- `ClockTreeViewBuilder` derives routed/flyline segments with `Router::buildClockNetTree` and pin-to-pin fallback logic (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:96`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:105`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:107`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:147`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:148`).
- `ClockTreeViewBuilder` builds direct sink-domain, sink-domain H-tree, and source-to-root view records from root buffers, downstream nets, inserted objects, and topology levels (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:217`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:231`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:263`).
- `ClockTreeVisualizationModelBuilder` consumes `ClockTreeView`, then adds fallback route/pin segments, fallback insts, logic-cell geometry, inst geometry, and pin markers from `Design`/`Wrapper` (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:330`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:337`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:338`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:340`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:341`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:342`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:343`).

Implication for `ClockTree`: stable role/domain/object/topology-level facts can move toward database, but segment geometry, flyline/design view modes, fallback warnings, layer/palette logic, file paths, and status tables must remain report/visualization.

#### Synthesis Produces Temporary Objects and Algorithm State

- `ClockSynthesis::BuildResult` owns temporary inserted `Inst`/`Pin`/`Net` objects and algorithm metadata until commit (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:89`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:95`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:96`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:104`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:105`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:106`).
- `ClockSynthesis::SourceToRootBuildResult` likewise owns temporary objects and source-to-root dispatch state (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:111`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:115`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:121`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:122`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:123`).
- `HTreeBuilder::BuildResult` carries many algorithm details: topology tree, level plans, selected characterization/pattern, grid/search counts, boundary fallback, temporary objects, and root pins/nets (`src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:101`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:107`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:108`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:109`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:110`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:123`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:145`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:146`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:147`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:151`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:152`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:153`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:154`).
- `HTreeCandidateTypes.hh` contains depth/candidate/evaluation structs that are algorithm search state, not design state (`src/operation/iCTS/source/flow/htree/HTreeCandidateTypes.hh:39`, `src/operation/iCTS/source/flow/htree/HTreeCandidateTypes.hh:61`, `src/operation/iCTS/source/flow/htree/HTreeCandidateTypes.hh:102`, `src/operation/iCTS/source/flow/htree/HTreeCandidateTypes.hh:111`).
- `HTreeCharacterizationTypes.hh` contains characterization grid/resolved option/flow result structs, not design state (`src/operation/iCTS/source/flow/htree/HTreeCharacterizationTypes.hh:38`, `src/operation/iCTS/source/flow/htree/HTreeCharacterizationTypes.hh:54`, `src/operation/iCTS/source/flow/htree/HTreeCharacterizationTypes.hh:61`).
- H-tree object builders create temporary buffers/nets with `std::make_unique`, record topology levels, and connect result-local nets before final commit (`src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:58`, `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:62`, `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:76`, `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:81`, `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:93`, `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:145`, `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:153`).
- H-tree synthesis selects candidate depth and pattern, then calls object construction and summary logging (`src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:173`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:185`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:218`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:252`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:281`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:291`).
- `ClockTreeSynthesisMetrics` transfers temporary object ownership from H-tree/segment results into higher-level synthesis results (`src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc:32`, `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc:42`, `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc:53`, `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc:64`, `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc:102`, `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc:112`, `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc:122`).
- `ClockTreeViewAdapter` currently adapts synthesis result temporary pointers into narrow view input records before commit (`src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.cc:86`, `src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.cc:95`, `src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.cc:104`, `src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.cc:118`, `src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.cc:127`, `src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.cc:137`).

Implication for `ClockTree`: do not promote `HTreeBuilder::BuildResult`, `LevelPlan`, candidate structs, characterization structs, synthesis metrics, cluster results, or temporary unique_ptr bundles into database. At most, after instantiation succeeds, persist the selected design-facing facts derived from them: object roles, domain membership, topology level/depth/count, and source-to-root versus sink-domain classification.

#### Instantiation/Commit Boundary

- `ClockSinkDomainBuilder` prepares hard-macro/regular sink domains by creating root buffers and downstream nets (`src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh:55`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:47`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:55`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:69`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:76`).
- `ClockTreeSynthesisTransaction::rollbackClock` restores the clock source net and clears committed CTS membership (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:119`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:121`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:122`).
- `ClockTreeSynthesisTransaction::commitSinkDomain` builds a pending view, then commits temporary objects through `ClockNetEditor::commitInsertedObjects`; on failure it reconnects/rolls back (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:160`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:163`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:165`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:167`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:169`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:173`).
- Source-to-root synthesis follows the same pattern: build result, adapt to view, commit inserted objects, then merge the view and record summary (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:241`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:254`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:257`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:267`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:268`).
- `ClockNetEditor::commitInsertedObjects` validates duplicate inst/pin/net names, moves objects into `Design`, and updates `Clock` membership (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:444`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:448`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:464`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:486`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:502`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:506`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:511`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:519`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:530`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:535`).
- iDB writeback remains a separate step through `WRAPPER_INST.writeClocks(clocks)` (`src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:34`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:40`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:48`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:49`).

Implication for `ClockTree`: creation/commit/reconnect/writeback operations belong to instantiation/netlist/writeback code. `ClockTree` can be updated only after successful commit or as an all-or-nothing transaction participant; it should not call `Design::commit*`, `ClockNetEditor`, `Wrapper`, or `STAAdapter`.

#### Evaluation and Report Consumers

- `ClockTreeEvaluator` reads committed `DESIGN_INST.get_clocks()`, clock insts, clock nets, and source nets; it computes area/wirelength/timing/RC metrics (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:443`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:450`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:467`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:483`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:488`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:499`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:507`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:510`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:512`).
- Evaluation has its own result models: `ClockTreeSummary`, `ClockTreeEvaluationState`, and `CTSStatistics` (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:35`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:90`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh:47`).
- Statistics writer owns report file rows and output file names (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:131`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:146`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:148`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:150`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:155`).
- Report step resolves report directories, rebuilds/reuses evaluation, writes statistics, and calls SVG/GDS visualization emitters (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:43`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:51`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:62`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:86`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:100`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:103`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:105`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:106`).
- SVG/GDS visualization consumes `ClockTreeView`, builds a report model, and writes files (`src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:427`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:430`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:431`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:445`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:446`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:170`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:179`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:191`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:193`).
- Visualization layer policy is report/display state: palette, layer kind, display names, and per-view layer keys (`src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh:37`, `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh:47`, `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh:57`, `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.cc:35`, `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.cc:178`).
- `CTSAPI::outputSummary()` adapts evaluation summary into `ieda_feature::CTSSummary`, whose fields are QoR/report summary data, not design data (`src/operation/iCTS/api/CTSAPI.cc:41`, `src/operation/iCTS/api/CTSAPI.cc:93`, `src/operation/iCTS/api/CTSAPI.cc:95`, `src/feature/database/feature_icts.h:25`).
- Platform `CtsIO::runCTS` and `reportCTS` call the API, while GUI tree data currently wraps iSTA clock-tree data rather than current flow `ClockTreeView` (`src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:33`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:44`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:45`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:53`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:58`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:165`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:169`, `src/interface/gui/interface/icts_io.cpp:29`).

Implication for `ClockTree`: evaluation/report may read `ClockTree` to avoid reclassifying stable roles, but their output summaries, timing/skew/wirelength metrics, statistics rows, visualization segment models, GDS layers, SVG/GDS paths, and feature summary compatibility fields must remain outside `ClockTree`.

### Exact `database/design/ClockTree` Responsibility

Data belongs in `database/design/ClockTree` only when all of these are true:

1. It is a CTS design fact about a clock tree, not a synthesis attempt, commit operation, evaluation result, or report artifact.
2. Its lifetime is tied to the `Design`/`Clock` topology lifecycle.
3. It can be represented using CTS database types, borrowed `Clock`/`Inst`/`Pin`/`Net` pointers, and/or stable object names.
4. At least two downstream layers can reasonably consume it, for example instantiation, evaluation, report, visualization, or feature extraction.
5. It is stable after successful instantiation/commit, or it is a coarse intended-tree fact that can be atomically discarded on synthesis/commit failure.

The stable data set should be:

- Tree identity:
  - associated `Clock*` or clock name/net name,
  - clock source pin and clock source net through the associated `Clock`,
  - optional stable tree id only if names are insufficient for multi-clock lookup.
- Sink-domain records:
  - domain kind: `hard_macro`, `regular`, `source_to_root`,
  - original sink/load pins for the domain,
  - root buffer inst and its input/output pins for downstream sink domains,
  - downstream net for each sink domain,
  - root-input pins for source-to-root connectivity.
- Object role index:
  - inserted CTS insts with role: root buffer, tree buffer, source-root buffer, clock load marker when needed,
  - CTS nets with role: clock source/source-to-root, downstream/root, sink-tree,
  - each role keyed by domain kind and associated clock.
- Stable selected topology metadata:
  - topology kind/section at a coarse level, such as direct, H-tree, source-to-root segment, source-to-root H-tree,
  - selected topology depth and level count,
  - per-inserted-buffer/per-inserted-net topology level and optional index in level,
  - optional parent/child or driver/load semantic adjacency only when netlist driver/load edges are insufficient for future source-to-sink traversal.
- Stable membership/query indexes:
  - query nets/insts by domain and role,
  - query topology level for a committed net/inst,
  - query root anchors and sinks for each domain,
  - query source-to-root path anchors.
- Minimal lifecycle/provenance:
  - If needed, one coarse tree-level state such as `planned`, `instantiated`, `written_to_external_db`.
  - Do not add per-object status flags, report statuses, failure reasons, or runtime booleans. Current flow-level `synthesis_complete`, `writeback_done`, and `evaluation_ready` belong in flow/instantiation state unless a real cross-module invariant requires a database-level lifecycle enum.

Data that should not be in `ClockTree`:

- DBU per micron unless `Design` lacks a single design-unit source. Existing consumers can query DBU through `Wrapper` or pass it into report models.
- `clock_index` used for report ordering or palette assignment. Report can assign a display index while iterating clocks.
- Geometry/flyline segment lists derived from `Router::buildClockNetTree` or pin-to-pin fallback. These are report/evaluation derivations over `Net`/routing data.
- GDS/SVG display modes, layer keys, colors, layer display names, file paths, report statuses, or output directory choices.
- Timing, latency, skew, slack, wirelength, area, capacitance, RC tree update status, statistics rows, or feature summary compatibility aliases.
- H-tree characterization, best char/pattern, frontier pools, depth candidate evaluation, legality context, grid plans, fallback scores, failure reasons, or debug counters.
- Temporary `std::unique_ptr<Inst/Pin/Net>` object bundles before final commit.
- Object creation, object naming policy execution, reconnect/rollback/writeback operations, or adapter access to iDB/iSTA.

### What Remains in Each Layer

#### Synthesis

Keep these in `flow/synthesis`, `flow/htree`, or synthesis-internal submodules:

- Options, runtime config resolution, H-tree build policy, source-to-root dispatch, and sink clustering.
- `ClockSynthesis::BuildResult` and `SourceToRootBuildResult` as temporary producer result bundles.
- Temporary inserted objects before commit.
- `HTreeBuilder::BuildResult`, `LevelPlan`, best characterization/pattern, selected/fallback metrics, depth/candidate/frontier state, characterization grid state, and sink-load profile legality context.
- Side-effect guards that temporarily reconnect root/source nets during algorithm trials.
- Synthesis failure reasons, log contexts, candidate counts, boundary fallback reasons, and debug/report summaries.
- Narrow adapter structs that bridge synthesis results into the stable database or report model.

`ClockTree` may receive the stable projection of a successful synthesis result, but it should not expose or store the synthesis result itself.

#### Instantiation

Keep these in the future `flow/instantiation` layer or existing netlist/writeback boundary until renamed:

- Creation of root buffers, downstream nets, source-to-root buffers, and temporary CTS objects.
- Object naming policy and safe-name generation.
- Reconnecting existing source/downstream nets.
- Validating duplicate names before commit.
- Moving `std::unique_ptr` temporary objects into `Design`.
- Updating `Clock` membership.
- All rollback logic on failed commit.
- iDB writeback through `Wrapper`.

Instantiation should be the only layer allowed to mutate `Design`/`ClockTree` with committed CTS topology. A good commit order is: validate temporary bundle -> commit to `Design` -> update `Clock` membership -> update `ClockTree` semantic records -> write iDB when requested. If any step before the stable update fails, discard/rollback the whole transaction.

#### Evaluation

Keep these in `flow/evaluation`:

- `ClockTreeSummary`, `ClockTreeEvaluationState`, `CTSStatistics`, and statistics report models.
- STA timing refresh, propagated clocks, latency/skew/timing queries, and `TimingEngine`/RC tree updates.
- Wirelength/HPWL/area/cell distribution calculations.
- Report-only compatibility aliases such as `buffer_num`, `max_level_of_clock_tree`, and `total_clock_wirelength`.
- Evaluation warnings/diagnostics and statistics file emission.

Evaluation can use `ClockTree` roles to avoid local reclassification of source/trunk/leaf nets, but evaluated values must remain evaluation results.

#### Report and Visualization

Keep these in `flow/report` or visualization/report submodules:

- Directory resolution and save paths.
- Rebuild/reuse evaluation policy.
- SVG/GDS model construction.
- Routed/flyline segment derivation and fallbacks.
- Logic cell/background geometry collection from `Wrapper`.
- Pin marker generation.
- GDS layer keys, palette/color policy, layer display names, and `.lyp` output.
- Report tables, report status rows, and file writers.

`ClockTreeView` can remain as a report adapter/model or be rebuilt from `ClockTree + Design`; it should not become the database object wholesale.

### Avoiding a Bag of Tiny Structs

Do not move the current set of view/adapter structs directly into `database/design`. In particular, avoid promoting separate public types like `ClockTreeViewInstTopology`, `ClockTreeViewNetTopology`, `ClockSinkDomainViewInput`, `ClockSourceToRootViewInput`, `ClockTreeVisualizationSegment`, `ClockTreeVisualizationInst`, and visualization layer keys into the database. They are useful adapter/report shapes, not stable design concepts.

Recommended shape:

- One primary class: `ClockTree`.
- A small set of coarse nested enums:
  - `DomainKind`
  - `InstRole`
  - `NetRole`
  - optional `TopologyKind` or `TreeSection`
  - optional `LifecycleState`
- At most three coarse public records:
  - `DomainRecord`: domain kind, sinks, root anchors, downstream/source-to-root anchors, and domain-level topology summary.
  - `ObjectRecord`: borrowed `Inst*` or `Net*`, object role, domain kind, topology level, and index-in-level. If C++ style makes a variant awkward, expose query methods instead of exposing separate tiny inst/net structs.
  - `TopologyRecord`: selected topology kind/depth/level count and optional semantic adjacency when needed for traversal.
- Private indexes inside `ClockTree`:
  - by domain kind,
  - by `Inst*`,
  - by `Net*`,
  - by role/domain pair.
- Public behavior-oriented methods instead of public bags:
  - `ensureDomain(kind)`
  - `recordDomainRoot(...)`
  - `recordInstRole(...)`
  - `recordNetRole(...)`
  - `recordTopologyLevel(...)`
  - `domains() const`
  - `instRole(inst) const`
  - `netRole(net) const`
  - `nets(domain, role) const`
  - `insts(domain, role) const`

This keeps the database model cohesive: the public contract is "query the stable CTS tree," not "assemble many small structs yourself." Tiny record types can still exist privately inside `.cc` files or report/synthesis adapter code where their scope is local.

### Suggested Migration Boundary

1. Add `database/design/ClockTree` as a stable semantic model with borrowed pointers and no adapter dependencies.
2. Move stable enums from `flow/clock_tree_view` to database only if they are renamed away from report wording. For example, prefer `ClockTree::NetRole` and `ClockTree::DomainKind` over `CTSNetRole`/`CTSSinkDomain` only after consumers are migrated.
3. During instantiation commit, update `ClockTree` after `Design::commit*` succeeds. Do not let synthesis update it directly with uncommitted temporary ownership.
4. Change evaluation/report to read stable roles/domains from `ClockTree` where useful.
5. Keep `ClockTreeView` or a replacement `ReportClockTreeModel` as a report adapter that derives segments, geometry, display indexes, modes, and paths from `ClockTree + Design + Wrapper`.
6. Keep external feature summary and GUI/STA tree consumers unchanged unless a separate task explicitly migrates them.

### External References

- No external references were used for this source-boundary research.
- Related existing research artifacts consulted:
  - `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/current-cts-flow-code-map.md`
  - `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/industry-cts-flow-terminology.md`
  - `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/open-source-cts-comparison.md`

### Related Specs

- `.trellis/spec/backend/database-guidelines.md` - establishes `Design` ownership of final `Clock`, `Inst`, `Pin`, and `Net`; `Clock` as borrowed membership view; temporary algorithm-local objects before commit; readonly evaluation/report consumers; external adapter boundaries.
- `.trellis/spec/backend/index.md` - points CTS flow refactors to database, directory, and quality guidelines.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - relevant because `ClockTree` crosses database, synthesis, instantiation, evaluation, report, Wrapper, and STAAdapter boundaries.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - relevant because the current view/model/adapter shapes should not be duplicated into another public bag of structs.

## Caveats / Not Found

- No existing `src/operation/iCTS/source/database/design/ClockTree.hh` or `ClockTree.cc` exists at inspection time.
- The current GUI CTS tree path wraps iSTA `StaClockTree` data through platform `CtsIO`; it is not currently a consumer of `flow/clock_tree_view`.
- Current evaluation still reclassifies nets as source/trunk/leaf locally. A future `ClockTree` can reduce that duplication, but evaluation metrics remain evaluation-owned.
- The exact physical owner can be implemented as `Design` owning per-clock `ClockTree` objects or `Clock` embedding/owning a `ClockTree` value. The boundary requirement is that lifetime follows `Design`/`Clock` reset and all `Inst`/`Pin`/`Net` links remain borrowed.
- No source code was modified for this research.
