# Research: synthesis-htree-characterization

- Query: Research the current iCTS synthesis/HTree/characterization implementation for the CTS engineering refactor. Inspect `src/operation/iCTS/source/flow/synthesis`, `src/operation/iCTS/source/flow/htree`, `src/operation/iCTS/source/module/characterization`, and related tests. Identify business concepts, lifecycle, data ownership, coupling hotspots, and refactor boundaries that preserve behavior.
- Scope: internal
- Date: 2026-04-28

## Findings

### Files Found

#### Workflow and Specs

- `.trellis/workflow.md` - Trellis workflow; research artifacts must be persisted under the task directory.
- `.trellis/spec/backend/index.md` - Backend spec index for iCTS.
- `.trellis/spec/backend/directory-structure.md` - Layering rule: flow orchestration belongs under `source/flow`, algorithms under `source/module`, shared data/adapters under `source/database`.
- `.trellis/spec/backend/database-guidelines.md` - Ownership and singleton boundary rules; `Design` owns final CTS objects and algorithm results may temporarily own objects before commit.
- `.trellis/spec/backend/quality-guidelines.md` - Naming, includes, CMake dependency visibility, and validation expectations.
- `.trellis/spec/backend/logging-guidelines.md` - `LOG_*` and schema/report output rules.
- `.trellis/spec/backend/error-handling.md` - No-exception policy and return-vs-fatal guidance.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - Relevant because this flow crosses Config, Wrapper, STAAdapter, Design, module, and flow boundaries.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - Relevant because several helper families are duplicated across synthesis, HTree, and segment materialization.

#### Synthesis Flow

- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh` - Public flow facade for downstream sink synthesis and source-to-root synthesis.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc` - Orchestrates optional sink clustering, HTree build, top-level source-to-root build, temporary net rewiring, rollback, and ownership transfer.
- `src/operation/iCTS/source/flow/synthesis/CMakeLists.txt` - Builds `icts_source_flow_synthesis` and links database, module, HTree flow, and utils.

#### HTree Flow

- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh` - Public HTree build facade, options, level plan, and result contract.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` - End-to-end HTree lifecycle: topology, characterization, candidate exploration, selection, materialization, root sizing, summary.
- `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh` - Large internal contract header containing option/result structs, registries, caches, combiner classes, materialization context, and helper declarations.
- `src/operation/iCTS/source/flow/htree/CharacterizationLibrary.hh` - Reusable `CharBuilder` cache interface.
- `src/operation/iCTS/source/flow/htree/CharacterizationLibrary.cc` - Runtime `CharBuilder::InitOptions` assembly from `CONFIG_INST` and cache-key matching.
- `src/operation/iCTS/source/flow/htree/SegmentBuilder.hh` - Public source-to-sink segment builder facade.
- `src/operation/iCTS/source/flow/htree/SegmentBuilder.cc` - Builds a single source-to-root segment using characterization frontiers and materializes optional buffers/nets.
- `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc` - HTree characterization-grid planning and `CharacterizationLibrary::ensure` orchestration.
- `src/operation/iCTS/source/flow/htree/HTreeBuildOptions.cc` - Resolves boundary options against characterization lattices.
- `src/operation/iCTS/source/flow/htree/HTreeLevelPlan.cc` - Converts topology levels into required length bins and resolves depth candidates.
- `src/operation/iCTS/source/flow/htree/HTreeSegmentFrontier.cc` - Builds base/synthesized segment frontiers needed for selected HTree lengths.
- `src/operation/iCTS/source/flow/htree/HTreeComposition.cc` - Composes segment frontiers into HTree topology frontiers and selects best/fallback entries.
- `src/operation/iCTS/source/flow/htree/HTreeDepthExploration.cc` - Iterates depth candidates and accumulates global feasible/candidate pools.
- `src/operation/iCTS/source/flow/htree/HTreeDepthCandidateEvaluation.cc` - Evaluates one candidate depth and records candidate summaries.
- `src/operation/iCTS/source/flow/htree/HTreeActualLoad.cc` - Actual external-load legality, load-cap coverage, fanout/cap/routing lower-bound checks.
- `src/operation/iCTS/source/flow/htree/HTreeMaterialization.cc` - Materializes selected patterns into temporary CTS insts, pins, nets; applies root-driver sizing and leaf-buffer pruning.
- `src/operation/iCTS/source/flow/htree/HTreeBuildSummary.cc` - Emits selected HTree build summary tables.
- `src/operation/iCTS/source/flow/htree/HTreeLogging.cc` - HTree-specific schema table/log formatting helpers.
- `src/operation/iCTS/source/flow/htree/CMakeLists.txt` - Builds `icts_source_flow_htree` from the split HTree translation units.

#### Characterization Module

- `src/operation/iCTS/source/module/characterization/CharBuilder.hh` - Stateful segment-characterization builder contract and state layout.
- `src/operation/iCTS/source/module/characterization/CharBuilder.cc` - Empty translation-unit anchor.
- `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc` - Buffer discovery, limit resolution, lattice setup, and setup reporting.
- `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc` - Sweep driver over wirelength points and progress/result reporting.
- `src/operation/iCTS/source/module/characterization/CharBuilderPatternEnumeration.cc` - Topology-slot and monotonic buffer-combination enumeration.
- `src/operation/iCTS/source/module/characterization/CharBuilderTopology.cc` - Converts topology bitsets into segment wire descriptions and terminal branch-buffer flags.
- `src/operation/iCTS/source/module/characterization/CharBuilderFeasibility.cc` - Electrical feasibility checks using buffer drive/input caps and wire capacitance.
- `src/operation/iCTS/source/module/characterization/CharBuilderCircuit.cc` - Temporary STA characterization circuit/net/parasitic creation and cleanup.
- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc` - Per-topology STA/iPA sampling context setup and load iteration.
- `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc` - Per-load slew sampling and `SegmentChar` emission.
- `src/operation/iCTS/source/module/characterization/CharBuilderSampleStorage.cc` - Lattice-index validation and overflow tracking for samples.
- `src/operation/iCTS/source/module/characterization/CharBuilderPatternStorage.cc` - Stores `BufferingPattern` metadata and normalized buffer positions.
- `src/operation/iCTS/source/module/characterization/Frontier.hh` - Frontier grouping/pruning helpers that preserve terminal semantics and monotonic boundary state.
- `src/operation/iCTS/source/module/characterization/HashJoinEngine.hh` - Header-only hash-join composition engine.
- `src/operation/iCTS/source/module/characterization/SegmentTraits.hh` - Segment hash-join keys and composition hook.
- `src/operation/iCTS/source/module/characterization/HTreeTraits.hh` - HTree hash-join keys with binary fanout half-cap transform.
- `src/operation/iCTS/source/module/characterization/SegmentCharTable.hh` - Thin table wrapper for segment hash-join composition.
- `src/operation/iCTS/source/module/characterization/HTreeTopologyCharTable.hh` - Thin table wrapper for HTree hash-join composition.
- `src/operation/iCTS/source/module/characterization/PatternCombiner.hh` - Generic pattern-ID combiners used by low-level tests.
- `src/operation/iCTS/source/module/characterization/CMakeLists.txt` - Builds `icts_source_module_characterization`; public dependency is characterization database types, private dependencies include STA adapter, config, utils, and logger.

#### Characterization Data Model

- `src/operation/iCTS/source/database/characterization/CharCore.hh` - Electrical boundary and cost carrier shared by segment and topology char entries.
- `src/operation/iCTS/source/database/characterization/SegmentChar.hh` - Segment characterization value type and segment composition formula.
- `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh` - HTree topology characterization value type and binary-fanout power composition formula.
- `src/operation/iCTS/source/database/characterization/BufferingPattern.hh` - Buffer positions, cell masters, terminal branch-buffer flag, and monotonic boundary state.
- `src/operation/iCTS/source/database/characterization/PatternId.hh` - Domain-tagged segment/topology pattern identifier.
- `src/operation/iCTS/source/database/characterization/ValueLattice.hh` - Uniform lattice for length/slew/cap discretization.
- `src/operation/iCTS/source/database/characterization/HTreeTopologyPattern.hh` - Compact HTree pattern metadata as level-to-segment-pattern references.

#### Related Lifecycle and Ownership Code

- `src/operation/iCTS/source/flow/FlowManager.cc` - Creates shared per-clock `CharacterizationLibrary`, invokes synthesis stages, and commits inserted objects.
- `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc` - Commits algorithm-owned inst/pin/net results into `Design` and clock membership.
- `src/operation/iCTS/source/database/design/Design.hh` - Public final-object creation/commit/index/removal API.
- `src/operation/iCTS/source/database/design/Design.cc` - Final CTS object ownership, name collision checks, pin indexing, and clock membership removal.

#### Related Tests

- `src/operation/iCTS/test/module/characterization/SegmentJoinTest.cc` - Segment join, monotonic boundary, frontier grouping, and switch-power accounting.
- `src/operation/iCTS/test/module/characterization/HTreeJoinTest.cc` - HTree half-cap join and binary-fanout power accounting.
- `src/operation/iCTS/test/module/characterization/BufferingPatternTest.cc` - BufferingPattern concat and downstream terminal branch-buffer semantics.
- `src/operation/iCTS/test/module/characterization/PrunerTest.cc` - Frontier dominance and preservation of distinct terminal/boundary states.
- `src/operation/iCTS/test/module/characterization/CharacterizationRealTechSmokeTest.cc` - Real-tech characterization smoke and manual HTree composition reports.
- `src/operation/iCTS/test/module/characterization/CharacterizationRealTechFallbackTest.cc` - Real-tech fallback, table-axis coverage, overflow reporting, and repeated reduced builds.
- `src/operation/iCTS/test/module/characterization/CharacterizationRealTechExactRegressionTest.cc` - Slow exact composition/power regression coverage.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderTest.cc` - HTree degenerate input behavior.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc` - Real-tech HTree smoke.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeSupport.cc` - Shared HTree real-tech assertions for pruning, depth coverage, and load distribution.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechBranchBufferRegressionTest.cc` - Branch-buffer policy, option override, top-boundary propagation, and fallback behavior.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc` - Synthesis invalid-input behavior, ownership/commit assumptions, config reporting, and source-to-root direct-connect behavior.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc` - Clustered real-tech synthesis behavior.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisNonClusteredRealTechSmokeTest.cc` - Non-clustered real-tech synthesis behavior.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechClusterValidation.cc` - Cluster buffer connectivity and master validation.
- `src/operation/iCTS/test/flow/synthesis/CMakeLists.txt`, `src/operation/iCTS/test/flow/htree/CMakeLists.txt`, `src/operation/iCTS/test/module/characterization/CMakeLists.txt` - Test target grouping and real-tech/slow-real-tech gates.

### Business Concepts

- `ClockSynthesis` is the caller-facing synthesis flow for one clock distribution. Its downstream path accepts a root net whose driver is the source/root-side pin and whose loads are final sink pins or cluster-center buffer inputs. It can optionally cluster sinks before invoking HTree; the source-to-root path connects the original clock source to one or more synthesized sink-group roots (`ClockSynthesis.hh:46`, `ClockSynthesis.hh:55`, `ClockSynthesis.hh:82`, `ClockSynthesis.hh:102`, `ClockSynthesis.hh:119`).
- Sink clustering introduces cluster-center buffers as local HTree loads, records `ClusterBufferMeta`, and creates per-cluster downstream sink nets (`ClockSynthesis.hh:62`, `ClockSynthesis.cc:435`, `ClockSynthesis.cc:455`, `ClockSynthesis.cc:458`, `ClockSynthesis.cc:459`, `ClockSynthesis.cc:468`).
- `HTreeBuilder` is the end-to-end HTree flow. Its public contract combines caller options, selected level plans, characterization metrics, candidate statistics, best char/pattern, actual-load distribution, root-driver sizing status, and temporary inserted objects (`HTreeBuilder.hh:57`, `HTreeBuilder.hh:73`, `HTreeBuilder.hh:87`, `HTreeBuilder.hh:131`, `HTreeBuilder.hh:135`).
- `LevelPlan` is the per-HTree-level binding between topology length in DBU/um, aligned characterization bin, selected segment pattern, and selected buffer semantics (`HTreeBuilder.hh:73`, `HTreeLevelPlan.cc:177`, `HTreeLevelPlan.cc:214`).
- `CharacterizationLibrary` is a flow-layer cache for one `CharBuilder` and its exact `InitOptions` request key. It avoids rebuilding when a request key matches and segment chars are already present (`CharacterizationLibrary.hh:34`, `CharacterizationLibrary.hh:37`, `CharacterizationLibrary.hh:47`, `CharacterizationLibrary.cc:53`, `CharacterizationLibrary.cc:56`).
- `CharBuilder` is the stateful segment-characterization engine. It resolves buffer inventory and lattices, enumerates segment buffering topologies, creates temporary STA/iPA circuits, samples timing/power, and stores `SegmentChar` plus `BufferingPattern` results (`CharBuilder.hh:56`, `CharBuilder.hh:59`, `CharBuilder.hh:77`, `CharBuilder.hh:80`, `CharBuilder.hh:108`, `CharBuilder.hh:183`, `CharBuilder.hh:200`, `CharBuilder.hh:211`).
- `SegmentBuilder` is a flow-level special case for a single source-to-root segment. It reuses the same characterization frontiers, filters by required sink load, source drive cap, optional soft input-slew boundary, and materializes a selected segment pattern (`SegmentBuilder.hh:45`, `SegmentBuilder.hh:55`, `SegmentBuilder.hh:77`, `SegmentBuilder.cc:318`, `SegmentBuilder.cc:402`, `SegmentBuilder.cc:428`).
- Characterization values are index-based, not physical-unit based. `CharCore` stores boundary bins, delay, power, pattern id, and source-boundary switching power (`CharCore.hh:36`, `CharCore.hh:41`, `CharCore.hh:55`). `UniformValueLattice` maps physical values to 1-based bins and rejects overflow via `tryObservedIndex` (`ValueLattice.hh:40`, `ValueLattice.hh:62`, `ValueLattice.hh:70`).
- `SegmentChar` composition is linear segment concatenation: upstream input/driven boundary, downstream output/load boundary, additive delay, and additive power minus downstream source-boundary switch power (`SegmentChar.hh:77`, `SegmentChar.hh:79`, `SegmentChar.hh:83`, `SegmentChar.hh:84`).
- `HTreeTopologyChar` composition models binary fanout: downstream power is doubled after subtracting downstream source-boundary switch power, and the half-cap join relationship is enforced by `HTreeTraits` (`HTreeTopologyChar.hh:83`, `HTreeTopologyChar.hh:91`, `HTreeTraits.hh:41`, `HTreeTraits.hh:43`, `HTreeTraits.hh:57`).
- `BufferingPattern` carries normalized buffer positions, cell masters, terminal branch-buffer semantics, and monotonic source/sink boundary state for safe composition (`BufferingPattern.hh:41`, `BufferingPattern.hh:55`, `BufferingPattern.hh:88`, `BufferingPattern.hh:93`, `BufferingPattern.hh:128`, `BufferingPattern.hh:133`).
- Pattern identity is domain-tagged to distinguish segment patterns from topology patterns (`PatternId.hh:34`, `PatternId.hh:46`, `PatternId.hh:54`, `PatternId.hh:59`).

### Lifecycle

1. `FlowManager` creates a per-clock `CharacterizationLibrary`, prepares sink-group root buffers, collects source-to-root lengths, runs downstream sink-group synthesis for each group, then runs top-level source-to-root synthesis (`FlowManager.cc:399`, `FlowManager.cc:401`, `FlowManager.cc:402`, `FlowManager.cc:410`).
2. Downstream synthesis enters `ClockSynthesis::build`. It validates root driver and loads, snapshots root-net and pin/driver naming side effects, normalizes valid loads, and resolves sink clustering from options or runtime config (`ClockSynthesis.cc:487`, `ClockSynthesis.cc:490`, `ClockSynthesis.cc:497`, `ClockSynthesis.cc:504`, `ClockSynthesis.cc:536`, `ClockSynthesis.cc:559`, `ClockSynthesis.cc:561`).
3. If clustering is enabled, `ClockSynthesis` derives clustering config from runtime config, runs default fast clustering, resolves the minimum legal cluster-buffer master, materializes cluster buffers/sink nets, and rewires the root net to the cluster buffer input pins (`ClockSynthesis.cc:567`, `ClockSynthesis.cc:568`, `ClockSynthesis.cc:569`, `ClockSynthesis.cc:574`, `ClockSynthesis.cc:580`, `ClockSynthesis.cc:585`).
4. `ClockSynthesis` builds `HTreeBuilder::BuildOptions` with min top input slew equal to half `max_buf_tran`, clustering-as-local-loads flag, shared library, extra characterization lengths, log context, and object name prefix (`ClockSynthesis.cc:357`, `ClockSynthesis.cc:360`, `ClockSynthesis.cc:364`, `ClockSynthesis.cc:365`, `ClockSynthesis.cc:366`, `ClockSynthesis.cc:367`).
5. `HTreeBuilder::build` validates root net driver and loads, opens a schema stage, queries DBU, generates topology, and stops before characterization when topology has no HTree levels (`HTreeBuilder.cc:81`, `HTreeBuilder.cc:86`, `HTreeBuilder.cc:95`, `HTreeBuilder.cc:101`, `HTreeBuilder.cc:102`, `HTreeBuilder.cc:107`, `HTreeBuilder.cc:121`).
6. HTree characterization planning collects topology level lengths plus caller-provided extra source-to-root lengths, adapts the wirelength grid if runtime config is missing/collapsed, calls `CharacterizationLibrary::ensure`, and records char limits/counters into the build result (`HTreeCharacterizationFlow.cc:80`, `HTreeCharacterizationFlow.cc:81`, `HTreeCharacterizationFlow.cc:82`, `HTreeCharacterizationFlow.cc:127`, `HTreeCharacterizationFlow.cc:136`, `HTreeCharacterizationFlow.cc:152`).
7. `CharBuilder::init` clears previous state, resolves sorted buffers and physical limits through STAAdapter queries, resolves wirelength/slew/cap lattices, sets routing layer/width, and emits setup reporting (`CharBuilderConfig.cc:370`, `CharBuilderConfig.cc:375`, `CharBuilderConfig.cc:398`, `CharBuilderConfig.cc:399`, `CharBuilderConfig.cc:400`, `CharBuilderConfig.cc:420`, `CharBuilderConfig.cc:422`, `CharBuilderConfig.cc:424`, `CharBuilderConfig.cc:437`).
8. `CharBuilder::build` initializes char-only STA context, loops wirelength bins, enumerates topology bitsets and monotonic buffer combinations, records progress, emits results, and calls `finishCharOnly` at the end (`CharBuilderBuild.cc:64`, `CharBuilderBuild.cc:79`, `CharBuilderBuild.cc:96`, `CharBuilderBuild.cc:98`, `CharBuilderBuild.cc:117`, `CharBuilderBuild.cc:181`, `CharBuilderBuild.cc:195`, `CharBuilderBuild.cc:199`).
9. Per topology, `CharBuilder` stores pattern metadata before feasibility/sampling, analyzes feasibility using wire cap and buffer limits, creates a temporary STA char circuit, samples loads and slews, emits `SegmentChar` entries, then destroys the temporary context (`CharBuilderSampling.cc:41`, `CharBuilderSampling.cc:51`, `CharBuilderSampling.cc:53`, `CharBuilderSampling.cc:67`, `CharBuilderCircuit.cc:38`, `CharBuilderStaSampling.cc:49`, `CharBuilderSlewSampling.cc:45`, `CharBuilderSlewSampling.cc:106`, `CharBuilderSlewSampling.cc:110`, `CharBuilderCircuit.cc:106`).
10. `HTreeBuilder` resolves build options, builds level plans and depth candidates, registers `CharBuilder` patterns, synthesizes segment frontiers for all required length bins, explores candidate depths, filters actual-load legality, selects the global best feasible entry or fallback, materializes selected patterns, optionally sizes the root driver, and logs the build summary (`HTreeBuilder.cc:138`, `HTreeBuilder.cc:143`, `HTreeBuilder.cc:151`, `HTreeBuilder.cc:159`, `HTreeBuilder.cc:164`, `HTreeBuilder.cc:173`, `HTreeBuilder.cc:178`, `HTreeBuilder.cc:185`, `HTreeBuilder.cc:233`, `HTreeBuilder.cc:251`, `HTreeBuilder.cc:280`, `HTreeBuilder.cc:283`, `HTreeBuilder.cc:290`).
11. HTree materialization walks selected topology levels bottom-up, creates inserted buffers/nets per segment pattern, rewires the input root net to root entry loads, and prunes redundant leaf single-load buffers (`HTreeMaterialization.cc:421`, `HTreeMaterialization.cc:427`, `HTreeMaterialization.cc:455`, `HTreeMaterialization.cc:461`, `HTreeMaterialization.cc:472`, `HTreeMaterialization.cc:496`, `HTreeMaterialization.cc:507`, `HTreeMaterialization.cc:510`, `HTreeMaterialization.cc:511`).
12. Source-to-root synthesis uses `ClockSynthesis::buildSourceToRoot`. It validates root inputs, temporarily reconnects the source net, uses `SegmentBuilder` for one root input, or `HTreeBuilder` with fixed root location and root-driver sizing disabled for multiple root inputs (`ClockSynthesis.cc:620`, `ClockSynthesis.cc:630`, `ClockSynthesis.cc:662`, `ClockSynthesis.cc:671`, `ClockSynthesis.cc:672`, `ClockSynthesis.cc:688`, `ClockSynthesis.cc:703`, `ClockSynthesis.cc:711`, `ClockSynthesis.cc:713`, `ClockSynthesis.cc:716`).
13. On successful synthesis, `FlowManager` commits `inserted_insts`, `inserted_pins`, and `inserted_nets` into final `Design` ownership through `ClockNetManager::commitInsertedObjects`; on commit failure it reconnects the downstream net for sink-group synthesis (`FlowManager.cc:128`, `FlowManager.cc:133`, `FlowManager.cc:135`, `FlowManager.cc:293`, `FlowManager.cc:302`).

### Data Ownership

- Algorithm build results temporarily own inserted CTS objects as `std::unique_ptr<Inst>`, `std::unique_ptr<Pin>`, and `std::unique_ptr<Net>` (`ClockSynthesis.hh:97`, `HTreeBuilder.hh:131`, `SegmentBuilder.hh:70`).
- `HTreeBuilder::BuildResult` also carries borrowed root pointers back to the input root net/driver and optionally the root driver inst/input pin (`HTreeBuilder.hh:135`, `HTreeBuilder.hh:138`, `HTreeBuilder.cc:86`, `HTreeBuilder.cc:87`, `HTreeBuilder.cc:88`).
- `ClockSynthesis` absorbs HTree/segment result ownership into its own result by moving unique_ptrs and clearing source vectors. This keeps one final owner before commit (`ClockSynthesis.cc:404`, `ClockSynthesis.cc:414`, `ClockSynthesis.cc:421`, `ClockSynthesis.cc:428`).
- `ClusterBufferMeta` stores raw pointers to objects owned by `ClockSynthesis::BuildResult::inserted_*`; those raw pointers are valid only while the owning result or committed `Design` objects outlive them (`ClockSynthesis.hh:62`, `ClockSynthesis.hh:67`, `ClockSynthesis.hh:70`, `ClockSynthesis.cc:459`).
- Final `Design` ownership is established only by commit APIs. `Design::commitInst`, `commitPin`, and `commitNet` reject name/full-name collisions and move the unique_ptr into final containers (`Design.hh:69`, `Design.hh:72`, `Design.hh:77`, `Design.cc:184`, `Design.cc:259`, `Design.cc:343`).
- `ClockNetManager::commitInsertedObjects` pre-validates duplicate algorithm names and existing final names before moving objects. It adds committed insts/nets to `Clock` membership but does not make `Clock` own the objects (`ClockNetManager.cc:428`, `ClockNetManager.cc:432`, `ClockNetManager.cc:448`, `ClockNetManager.cc:470`, `ClockNetManager.cc:486`, `ClockNetManager.cc:490`, `ClockNetManager.cc:495`, `ClockNetManager.cc:503`, `ClockNetManager.cc:514`, `ClockNetManager.cc:519`).
- `Design::removeClockMembershipObjects` removes final objects referenced by a clock membership list while preserving the clock source net (`Design.cc:438`, `Design.cc:440`, `Design.cc:441`, `Design.cc:448`).
- `CharBuilder` owns characterization data vectors and temporary STA object names, not final CTS design objects. Temporary external STA state is managed through `STA_ADAPTER_INST` char-context calls (`CharBuilder.hh:206`, `CharBuilder.hh:211`, `CharBuilderCircuit.cc:40`, `CharBuilderCircuit.cc:65`, `CharBuilderCircuit.cc:106`, `CharBuilderBuild.cc:96`, `CharBuilderBuild.cc:199`).
- `CharacterizationLibrary` owns one `CharBuilder`, a request key, and a ready flag. Consumers borrow the `CharBuilder` through `getCharBuilder()` (`CharacterizationLibrary.hh:47`, `CharacterizationLibrary.hh:48`, `CharacterizationLibrary.hh:73`, `CharacterizationLibrary.hh:75`).
- `Tree` topology ownership stays inside `HTreeBuilder::BuildResult::topology`; materialization and actual-load checks borrow tree nodes and load vectors during the build (`HTreeBuilder.hh:93`, `HTreeActualLoad.cc:138`, `HTreeMaterialization.cc:427`).
- Pattern registries own per-build pattern metadata. `BufferPatternRegistry` owns segment `BufferingPattern` copies and composition-state cache; `TopologyPatternRegistry` owns topology pattern nodes and materializes a compact `HTreeTopologyPattern` value into the final result (`HTreeBuilderInternal.hh:242`, `HTreeBuilderInternal.hh:313`, `HTreeBuilderInternal.hh:371`, `HTreeBuilderInternal.hh:425`, `HTreeBuilder.cc:251`).

### Coupling Hotspots

- `HTreeBuilderInternal.hh` is the largest coupling surface. It includes logging, STAAdapter, CharBuilder, DB characterization types, HTree public API, and contains both pure algorithm state and adapter-backed caches (`HTreeBuilderInternal.hh:26`, `HTreeBuilderInternal.hh:54`, `HTreeBuilderInternal.hh:55`, `HTreeBuilderInternal.hh:60`, `HTreeBuilderInternal.hh:172`, `HTreeBuilderInternal.hh:546`, `HTreeBuilderInternal.hh:676`).
- `BufferStrengthCache` and `BufferPortCache` are internal helper classes in the HTree internal header, but they call `STA_ADAPTER_INST` directly, which makes all translation units including this header conceptually depend on STA access (`HTreeBuilderInternal.hh:172`, `HTreeBuilderInternal.hh:182`, `HTreeBuilderInternal.hh:546`, `HTreeBuilderInternal.hh:556`).
- Runtime config reads are scattered across flow files: sink clustering and min slew in `ClockSynthesis`, HTree topology tolerance/depth/window in `HTreeBuilder`/`HTreeLevelPlan`, force branch buffer in `HTreeBuildOptions`, actual-load electrical config in `HTreeActualLoad`, and `CharacterizationLibrary::buildRuntimeOptions` (`ClockSynthesis.cc:360`, `ClockSynthesis.cc:374`, `ClockSynthesis.cc:561`, `HTreeBuilder.cc:106`, `HTreeLevelPlan.cc:139`, `HTreeLevelPlan.cc:307`, `HTreeBuildOptions.cc:49`, `HTreeActualLoad.cc:92`, `CharacterizationLibrary.cc:74`).
- `CharBuilder` follows the module guideline by receiving runtime config as `InitOptions`, but it directly calls `STA_ADAPTER_INST` for liberty limits, ports, RC, temporary char netlist, timing, and power. This is adapter-facade access rather than raw external iSTA types, but it is still a strong module-to-singleton coupling (`CharBuilderConfig.cc:142`, `CharBuilderConfig.cc:162`, `CharBuilderConfig.cc:269`, `CharBuilderCircuit.cc:49`, `CharBuilderCircuit.cc:98`, `CharBuilderStaSampling.cc:54`, `CharBuilderSlewSampling.cc:62`, `CharBuilderSlewSampling.cc:88`).
- `CharacterizationLibrary` cache correctness is exact-key based, including optional doubles and vectors (`CharacterizationLibrary.hh:54`, `CharacterizationLibrary.hh:68`, `CharacterizationLibrary.cc:36`, `CharacterizationLibrary.cc:55`). `SegmentBuilder` calls `ensure` only when the shared library is not ready, so top-segment coverage depends on upstream HTree characterization including `additional_characterization_lengths_um` from `FlowManager` (`SegmentBuilder.cc:351`, `SegmentBuilder.cc:354`, `FlowManager.cc:400`, `FlowManager.cc:401`, `FlowManager.cc:126`). This is a behavior-preserving refactor constraint and a possible hidden contract.
- Net connection and temporary object materialization helpers are duplicated in `ClockSynthesis`, `SegmentBuilder`, and `HTreeMaterialization` with similar but not identical behavior around old net cleanup, owned vs existing nets, naming, and pin insertion (`ClockSynthesis.cc:201`, `ClockSynthesis.cc:222`, `ClockSynthesis.cc:253`, `SegmentBuilder.cc:69`, `SegmentBuilder.cc:97`, `SegmentBuilder.cc:114`, `SegmentBuilder.cc:136`, `HTreeMaterialization.cc:57`, `HTreeMaterialization.cc:80`, `HTreeMaterialization.cc:101`).
- `ClockSynthesis` temporarily mutates the input root net and root-driver inst/pin names while guarding rollback with a local lambda. This is correct but fragile because root-driver sizing can rename design-indexed pins (`ClockSynthesis.cc:504`, `ClockSynthesis.cc:520`, `ClockSynthesis.cc:536`, `ClockSynthesis.cc:545`, `ClockSynthesis.cc:551`, `ClockSynthesis.cc:597`, `ClockSynthesis.cc:609`).
- Root-driver sizing in `HTreeMaterialization` mixes selected topology semantics, STA buffer-port queries, design pin-index collision checks, pin renaming, inst type/master mutation, and root pin result updates (`HTreeMaterialization.cc:156`, `HTreeMaterialization.cc:172`, `HTreeMaterialization.cc:188`, `HTreeMaterialization.cc:204`, `HTreeMaterialization.cc:357`, `HTreeMaterialization.cc:387`, `HTreeMaterialization.cc:410`).
- Actual-load legality combines topology pattern metadata, HTree `Tree` loads, runtime clustering/electrical config, fanout/cap/routing legality, lattice coverage, memoization, and monotone hard-fail pruning in one file (`HTreeActualLoad.cc:90`, `HTreeActualLoad.cc:103`, `HTreeActualLoad.cc:138`, `HTreeActualLoad.cc:204`, `HTreeActualLoad.cc:231`, `HTreeActualLoad.cc:249`, `HTreeActualLoad.cc:280`, `HTreeActualLoad.cc:319`, `HTreeActualLoad.cc:342`, `HTreeActualLoad.cc:394`).
- Candidate selection is spread across composition, depth exploration, actual-load filtering, and final global selection. The selected entry is a pointer into a vector owned by a `CandidateBuildEvaluation`; this is safe within the current exploration object lifetime but should not be made to outlive it (`HTreeDepthCandidateEvaluation.cc:80`, `HTreeDepthCandidateEvaluation.cc:84`, `HTreeDepthExploration.cc:46`, `HTreeBuilder.cc:201`, `HTreeBuilder.cc:202`).
- Reporting is interleaved with algorithm decisions in several files. This preserves current diagnostics but makes pure unit testing of policy harder (`CharBuilderConfig.cc:437`, `CharBuilderBuild.cc:181`, `HTreeCharacterizationFlow.cc:124`, `HTreeBuildSummary.cc:57`, `SegmentBuilder.cc:271`, `ClockSynthesis.cc:273`).

### Behavior to Preserve

- Invalid downstream synthesis inputs must fail without inserted objects and without damaging existing clock membership (`ClockSynthesisTest.cc:133`, `ClockSynthesisTest.cc:149`, `ClockSynthesisTest.cc:165`, `ClockSynthesisTest.cc:177`).
- Design commit must reject final name/full-name collisions and leave existing objects unchanged (`ClockSynthesisTest.cc:202`, `ClockSynthesisTest.cc:218`, `ClockSynthesisTest.cc:222`).
- Final ownership must remain: `Design` owns committed insts/pins/nets, and `Clock` only tracks membership views (`ClockSynthesisTest.cc:232`, `ClockSynthesisTest.cc:263`, `ClockSynthesisTest.cc:267`).
- Source-to-root with no roots must restore the source net; a single root at the same location must directly connect without inserted objects (`ClockSynthesisTest.cc:334`, `ClockSynthesisTest.cc:350`, `ClockSynthesisTest.cc:356`, `ClockSynthesisTest.cc:367`).
- HTree degenerate cases must stop before topology/characterization/materialization as appropriate, retaining root pointer observations and leaving loads/pin nets stable (`HTreeBuilderTest.cc:55`, `HTreeBuilderTest.cc:78`, `HTreeBuilderTest.cc:114`, `HTreeBuilderTest.cc:135`).
- Segment join semantics must preserve exact boundary joins, monotonic boundary rejection, terminal semantics in frontier grouping, and downstream source-boundary switch-power subtraction (`SegmentJoinTest.cc:91`, `SegmentJoinTest.cc:163`, `SegmentJoinTest.cc:192`, `SegmentJoinTest.cc:219`).
- HTree join semantics must preserve half-cap keying, ceil half-cap behavior, binary-fanout power doubling, and source-boundary switch-power subtraction (`HTreeJoinTest.cc:40`, `HTreeJoinTest.cc:70`, `HTreeJoinTest.cc:92`, `HTreeJoinTest.cc:109`).
- `BufferingPattern::concat` must use downstream terminal branch-buffer semantics and renormalize positions (`BufferingPatternTest.cc:35`, `BufferingPatternTest.cc:53`).
- Frontier pruning must preserve distinct exact join boundaries and terminal semantics even when delay/power dominance would otherwise remove entries (`PrunerTest.cc:68`, `PrunerTest.cc:85`, `PrunerTest.cc:117`).
- Real-tech characterization must produce in-range lattice entries and positive power when assets support it (`CharacterizationRealTechSmokeTest.cc:70`, `CharacterizationRealTechSmokeTest.cc:78`, `CharacterizationRealTechSmokeTest.cc:80`, `CharacterizationRealTechSmokeTest.cc:83`).
- HTree real-tech behavior must preserve redundant leaf single-load buffer pruning, selected depth coverage, and selected real-load cap distribution reporting (`HTreeBuilderRealTechSmokeSupport.cc:181`, `HTreeBuilderRealTechSmokeSupport.cc:222`, `HTreeBuilderRealTechSmokeSupport.cc:239`).
- Branch-buffer policy must support config-driven forcing, caller override, feasible top-boundary filtering, and impossible top-boundary fallback with diagnostic score (`HTreeBuilderRealTechBranchBufferRegressionTest.cc:52`, `HTreeBuilderRealTechBranchBufferRegressionTest.cc:137`, `HTreeBuilderRealTechBranchBufferRegressionTest.cc:155`, `HTreeBuilderRealTechBranchBufferRegressionTest.cc:249`, `HTreeBuilderRealTechBranchBufferRegressionTest.cc:255`, `HTreeBuilderRealTechBranchBufferRegressionTest.cc:270`).
- Clustered synthesis must produce cluster metadata, cluster buffer count matching non-empty clusters, HTree min top input slew as half max slew, cluster buffer connectivity, and per-cluster fanout bounds (`ClockSynthesisRealTechSmokeTest.cc:107`, `ClockSynthesisRealTechSmokeTest.cc:119`, `ClockSynthesisRealTechSmokeTest.cc:124`, `ClockSynthesisRealTechSmokeTest.cc:128`, `ClockSynthesisRealTechSmokeTest.cc:131`, `ClockSynthesisRealTechSmokeTest.cc:135`).
- Non-clustered synthesis must skip cluster buffers/results while still building unrestricted HTree and preserving root-net driver/load connectivity (`ClockSynthesisNonClusteredRealTechSmokeTest.cc:93`, `ClockSynthesisNonClusteredRealTechSmokeTest.cc:99`, `ClockSynthesisNonClusteredRealTechSmokeTest.cc:100`, `ClockSynthesisNonClusteredRealTechSmokeTest.cc:106`, `ClockSynthesisNonClusteredRealTechSmokeTest.cc:110`).

### Refactor Boundaries That Preserve Behavior

- Preserve public contracts first: `ClockSynthesis::BuildOptions/BuildResult`, `SourceToRootBuildOptions/Result`, `HTreeBuilder::BuildOptions/BuildResult`, `SegmentBuilder::BuildOptions/BuildResult`, `CharacterizationLibrary::ensure`, and `CharBuilder::InitOptions` are all externally consumed inside the flow/tests (`ClockSynthesis.hh:46`, `ClockSynthesis.hh:82`, `ClockSynthesis.hh:102`, `HTreeBuilder.hh:57`, `HTreeBuilder.hh:87`, `SegmentBuilder.hh:45`, `SegmentBuilder.hh:55`, `CharacterizationLibrary.hh:37`, `CharBuilder.hh:59`).
- Keep value-object semantics stable. `CharCore`, `SegmentChar`, `HTreeTopologyChar`, `BufferingPattern`, `PatternId`, `HTreeTopologyPattern`, and `UniformValueLattice` are compact data/model types with direct unit-test coverage and low external coupling (`CharCore.hh:36`, `SegmentChar.hh:77`, `HTreeTopologyChar.hh:83`, `BufferingPattern.hh:133`, `PatternId.hh:46`, `HTreeTopologyPattern.hh:40`, `ValueLattice.hh:40`).
- A safe first extraction boundary is "runtime characterization request planning": keep `CharacterizationLibrary` as facade, but isolate assembly/coverage rules for `CharBuilder::InitOptions` from `CONFIG_INST`, topology lengths, and additional source-to-root lengths. Preserve exact cache-key behavior until tests are in place for reuse/coverage (`CharacterizationLibrary.cc:74`, `HTreeCharacterizationFlow.cc:80`, `HTreeCharacterizationFlow.cc:127`, `SegmentBuilder.cc:354`).
- A second safe boundary is "frontier algebra": `HashJoinEngine`, traits, frontier pruning, segment-frontier synthesis, and HTree composition are mostly pure and already unit-tested. They can be moved/split behind narrower headers if the PatternRegistry/CompositionState contracts remain unchanged (`HashJoinEngine.hh:95`, `Frontier.hh:197`, `HTreeSegmentFrontier.cc:329`, `HTreeComposition.cc:340`).
- Keep materialization in the flow layer, not the characterization module. It mutates CTS nets/pins and depends on final design naming constraints. If extracted, use a flow-local object factory/reconnector that explicitly owns result vectors and differentiates existing-net rewiring from owned-net creation (`HTreeMaterialization.cc:421`, `SegmentBuilder.cc:232`, `ClockSynthesis.cc:201`, `ClockSynthesis.cc:239`).
- Split `HTreeBuilderInternal.hh` by role before deeper behavior changes: candidate data structs, pattern registries/combiners, adapter-backed caches, actual-load legality data, and materialization context have different dependency needs. This can reduce build coupling without changing algorithms (`HTreeBuilderInternal.hh:89`, `HTreeBuilderInternal.hh:172`, `HTreeBuilderInternal.hh:242`, `HTreeBuilderInternal.hh:371`, `HTreeBuilderInternal.hh:570`, `HTreeBuilderInternal.hh:655`).
- Treat source-to-root `SegmentBuilder` reuse as a fragile contract. Today a ready shared `CharacterizationLibrary` is assumed to already cover requested top segment lengths because downstream HTree passed `additional_characterization_lengths_um`; any refactor should either preserve that call order or make coverage explicit (`FlowManager.cc:400`, `FlowManager.cc:401`, `FlowManager.cc:126`, `SegmentBuilder.cc:351`, `SegmentBuilder.cc:354`).
- Root-driver sizing should remain a distinct refactor boundary with explicit precheck/apply phases. The precheck protects design pin-index conflicts before materialization, and `ClockSynthesis` relies on rollback if HTree fails (`HTreeBuilder.cc:273`, `HTreeBuilder.cc:274`, `HTreeMaterialization.cc:357`, `HTreeMaterialization.cc:387`, `ClockSynthesis.cc:536`).
- Actual-load legality can be extracted behind a service-like API that accepts topology, topology pattern registry, segment pattern registry, cap lattice, and a resolved electrical config. Preserve monotone hard-fail cache behavior and first failure reason because selection/fallback diagnostics depend on it (`HTreeActualLoad.cc:319`, `HTreeActualLoad.cc:325`, `HTreeActualLoad.cc:337`, `HTreeActualLoad.cc:342`, `HTreeActualLoad.cc:349`, `HTreeActualLoad.cc:369`).
- Logging/reporting extraction should preserve titles and key names used by real-tech tests and artifact consumers. Many tests search for specific report fields or absence of duplicated fields (`ClockSynthesisTest.cc:310`, `ClockSynthesisTest.cc:315`, `ClockSynthesisRealTechSmokeTest.cc:140`, `HTreeBuilderRealTechBranchBufferRegressionTest.cc:104`, `HTreeBuilderRealTechBranchBufferRegressionTest.cc:106`).

### Related Specs

- `.trellis/spec/backend/directory-structure.md` - Flow/module/database boundaries, CMake target placement, and config access guidance.
- `.trellis/spec/backend/database-guidelines.md` - Ownership rules for `Design`, `Clock`, algorithm-local temporary objects, borrowed pointers, `Tree`, `Wrapper`, and `STAAdapter`.
- `.trellis/spec/backend/logging-guidelines.md` - Required `LOG_*` and schema/report ownership conventions.
- `.trellis/spec/backend/error-handling.md` - Safe-return vs fatal rules for missing inputs and invalid internal state.
- `.trellis/spec/backend/quality-guidelines.md` - Include/dependency/naming expectations for any future split.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - Apply before changes touching Config, Wrapper, STAAdapter, Design, topology, and characterization boundaries.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - Apply before extracting shared net/object materialization, characterization request setup, or CMake wiring.

### External References

- No external references were used. This research is based on repository code, tests, and Trellis specs only.

## Caveats / Not Found

- I did not modify production code and did not run tests; this artifact is code-reading research only.
- I did not run any git commands because the active role is the Trellis research agent and the research scope forbids git operations.
- I inspected the requested production directories completely at the file-list level and read the core implementation files. I used targeted reads for related real-tech matrix/artifact helpers instead of exhaustively analyzing every artifact renderer.
- No PRD was present in the active task at the time of this research; this artifact should be linked from future `implement.jsonl`/`check.jsonl` once the PRD/context files are curated.
- The main not-found item is direct unit coverage for `CharacterizationLibrary` cache reuse/coverage. Behavior is indirectly exercised through HTree/source-to-root flows, but the exact "ready shared library already covers requested top length" contract is not isolated in a small test.
