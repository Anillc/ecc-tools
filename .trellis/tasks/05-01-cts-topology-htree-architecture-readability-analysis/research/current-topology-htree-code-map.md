# Current Topology and H-Tree Code Map

## Scope

This note analyzes the current tree under:

```text
src/operation/iCTS/source/flow/synthesis/topology
```

The goal is not to re-litigate the accepted top-level flow architecture. The issue is narrower: the `synthesis/topology` layer and nested `topology/htree` internals are now too deep and not self-explanatory enough for readers.

## Current Shape

Current file distribution:

```text
synthesis/topology/                                      10 files
synthesis/topology/htree/                                 3 files
synthesis/topology/htree/characterization/                8 files
synthesis/topology/htree/construction/                    6 files
synthesis/topology/htree/pattern_search/                 12 files
```

Largest implementation files:

```text
568  htree/construction/HTreeClockTreeObjectBuilder.cc
467  htree/construction/SourceToRootSegmentBuilder.cc
430  htree/pattern_search/HTreeSinkLoadProfile.cc
408  htree/pattern_search/HTreeTopologyAssembly.cc
357  htree/pattern_search/HTreeSegmentCandidateFrontier.cc
355  htree/pattern_search/HTreePatternRegistry.hh
318  htree/pattern_search/HTreeLevelPlan.cc
300  htree/HTree.cc
274  topology/ClockTreeSynthesisTransaction.cc
252  htree/construction/ClockSynthesisNetEditor.cc
```

The deepest active implementation paths are five business/code levels below `flow`:

```text
flow/synthesis/topology/htree/characterization
flow/synthesis/topology/htree/pattern_search
flow/synthesis/topology/htree/construction
```

This is readable only if each intermediate directory has strong semantic value. Today, `topology` does not.

## Responsibility Map

### `topology/Topology.hh`

Current role:

* Nominal topology-family facade.
* Contains only a deleted `ClockTreeTopology` class.
* Does not guide readers to the actual primary interfaces.

Assessment:

* This is an empty taxonomy marker, not a useful module entry.
* It creates the expectation that `topology/` has a coherent public API, but the real API is elsewhere.

### `topology/ClockSynthesis.hh/.cc`

Current role:

* Public one-clock tree synthesis entry.
* Dispatches downstream sink-tree synthesis and source-to-root synthesis.
* Owns broad result records:
  * sink clustering result
  * H-tree result
  * inserted temporary objects
  * inserted object level metadata
  * source-to-root result

Assessment:

* This is not just topology. It is one-clock clock-tree synthesis orchestration.
* The name is understandable, but its directory is misleading.
* Its result structs are boundary records, but they are large because they combine algorithm result, construction artifacts, and reporting metrics.

### `topology/ClockSinkTreeSynthesizer.*`

Current role:

* Builds the downstream sink tree for a sink domain.
* Optionally handles clustering-generated local buffers.
* Calls H-tree build and construction helpers.

Assessment:

* This is a clock-tree synthesis sub-scenario, not a generic topology file.
* It reads better as part of a `clock_tree` or `tree` synthesis boundary than as a root-level `topology` peer.

### `topology/ClockSourceRootSynthesizer.*`

Current role:

* Builds the source-to-root path.
* Dispatches between a direct top segment and H-tree construction depending on root-input count.
* Uses source-net side-effect guards and temporary object construction.

Assessment:

* This is trunk/source path synthesis, not pure topology.
* The phrase `source_to_root` is accurate but still implementation-shaped. The CTS business concept is closer to a top trunk, source trunk, or clock-source trunk.
* Its current coupling to `htree/construction/SourceToRootSegmentBuilder` makes H-tree look responsible for a non-H-tree scenario.

### `topology/ClockTreeSynthesisTransaction.*`

Current role:

* Per-clock transaction boundary.
* Coordinates sink-domain synthesis, source-to-root synthesis, rollback, commit, clock membership updates, trace/status updates, and visualization model merge.

Assessment:

* This is not topology. It is synthesis lifecycle/transaction orchestration.
* Keeping it under `topology` makes the directory harder to read because it mixes algorithm shape selection with state mutation boundaries.
* It likely belongs near the one-clock synthesis orchestration module.

### `topology/htree/HTree.hh/.cc`

Current role:

* H-tree family entry.
* Builds topology, resolves characterization, searches depth candidates, selects a candidate, applies root-driver sizing, and builds temporary clock-tree objects.
* Public result record exposes topology, search, characterization, construction artifacts, metrics, and root object pointers.

Assessment:

* This is a real primary entry, but its public contract is too wide.
* The path is deeper than necessary because `topology` adds a taxonomy level above the real H-tree family.
* `HTreeBuilder::BuildResult` is a stable boundary only in the behavior-preserving sense; semantically it mixes at least four outputs:
  * selected topology shape
  * pattern-search diagnostics
  * characterization grid metadata
  * temporary construction artifacts

### `htree/characterization`

Current role:

* Characterization library/cache/result types.
* Runtime option adaptation for H-tree characterization.
* H-tree characterization flow.

Assessment:

* The directory name is good and EDA-friendly.
* The placement below `topology/htree` is the problem, not the local name.
* `ClockSynthesisHtreeOptions.*` sits here because it adapts runtime clock synthesis options into H-tree options. This is acceptable if H-tree remains a standalone family, but it reads as a cross-boundary adapter and should be treated as such.

### `htree/pattern_search`

Current role:

* Segment pattern collection.
* Segment frontier construction.
* Depth candidate search/evaluation.
* Topology assembly.
* Sink-load legality filtering.
* H-tree synthesis summary logging.
* Level-plan helper logic.

Assessment:

* The name is better than `selection`, but the directory is now too broad.
* `pattern_search` includes search, legality, assembly, and summary logging. Those are related, but they are not all the same responsibility.
* The heaviest files in the H-tree algorithm are here, so this directory becomes a dense catch-all.

### `htree/construction`

Current role:

* H-tree temporary object construction.
* Source-to-root segment builder.
* Temporary net/object editing and side-effect guards.

Assessment:

* `HTreeClockTreeObjectBuilder.*` fits here.
* `SourceToRootSegmentBuilder.*` is questionable because a top segment is not necessarily an H-tree topology.
* `ClockSynthesisNetEditor.*` is even broader. It creates temporary `Inst`/`Pin`/`Net` objects and guards net side effects for clock synthesis. That is a clock-tree construction utility, not H-tree-specific logic.

## Readability Failures

### 1. `topology` is an empty intermediate taxonomy

Readers enter `synthesis/topology` expecting a primary `Topology` API. Instead:

* `Topology.hh` is effectively empty.
* `ClockSynthesis.hh` is the real one-clock tree synthesis interface.
* `ClockTreeSynthesisTransaction.hh` is the real state mutation boundary.
* H-tree is the real topology-family implementation.

This means the directory name does not match the reader's path through the code.

### 2. The root files under `topology` are not all topology responsibilities

`ClockTreeSynthesisTransaction` and source-to-root synthesis are not topology search. They coordinate synthesis state and object lifecycle. Keeping them beside `Topology.hh` makes the local architecture look less deliberate than it is.

### 3. `topology/htree` creates avoidable depth

If there are multiple topology families, `topology/htree` makes sense. Current code has only H-tree. Therefore the `topology` layer mostly adds path length:

```text
synthesis/topology/htree/pattern_search/HTreeTopologyAssembly.cc
```

This path repeats the concept of topology three times: directory `topology`, directory `htree`, and file prefix `HTreeTopology`.

### 4. H-tree internals are locally coherent but still too dense

`characterization`, `pattern_search`, and `construction` are reasonable names. The issue is that `pattern_search` is overloaded with candidate pattern storage, depth evaluation, topology assembly, legality filtering, and logging.

### 5. Some broader clock-tree construction utilities are hidden under H-tree

`ClockSynthesisNetEditor` and `SourceToRootSegmentBuilder` are not obviously H-tree-only. This makes readers infer the wrong ownership boundary.

### 6. CMake boundaries do not fully mirror semantic boundaries

The synthesis target compiles some H-tree-adjacent files directly:

```text
topology/htree/characterization/ClockSynthesisHtreeOptions.cc
topology/htree/construction/ClockSynthesisNetEditor.cc
```

This is a smell: either those files are truly H-tree internals and should be owned by the H-tree target, or they are synthesis/clock-tree adapters and should not be placed under H-tree internals.

## Recommendation

Recommended direction: keep two synthesis subfolders, but make their semantics explicit:

* `topology/`: CTS topology formation boundary. It owns sink-side branch formation, source-side trunk formation, and the accepted topology result for one clock.
* `htree/`: concrete H-tree algorithm engine. It owns H-tree characterization, pattern search, and H-tree embedding.

This keeps the user's accepted two-folder split while avoiding a redundant `clock_tree` folder and avoiding arbitrary `Clock`/`Tree`/`Synthesis` name prefixes. Under `source/operation/iCTS`, `flow/synthesis` already tells readers that this is CTS clock-tree synthesis code; names below it should add only new business information.

Target shape:

```text
src/operation/iCTS/source/flow/synthesis/
  Synthesis.hh
  Synthesis.cc

  distribution/
    ClockDistribution.hh
    ClockDistribution.cc
    ...

  topology/
    Topology.hh
    Topology.cc
    sink/
      SinkBranch.hh
      SinkBranch.cc
    trunk/
      SourceTrunk.hh
      SourceTrunk.cc
    buffer/
      BufferInsertion.hh
      BufferInsertion.cc

  htree/
    HTree.hh
    HTree.cc
    characterization/
      Characterization.hh
      Characterization.cc
      CharacterizationLibrary.hh
      CharacterizationLibrary.cc
      Options.hh
      Options.cc
      ...
    pattern/
      PatternSearch.hh
      PatternSearch.cc
      PatternLibrary.hh
      SegmentFrontier.cc
      DepthSearch.cc
      DepthEvaluation.cc
      SinkLoadRegion.hh
      SinkLoadRegion.cc
      ...
    embedding/
      Embedding.hh
      Embedding.cc
      EmbeddingState.hh

  trace/
    SynthesisTrace.hh
    SynthesisTrace.cc
    ...
```

Why this is better:

* `topology/Topology.hh` becomes the real primary entry instead of an empty facade.
* `topology/sink/SinkBranch` describes the sink-side branch formation scenario without repeating `Clock`, `Tree`, or `Synthesis`.
* `topology/trunk/SourceTrunk` describes the source-to-root trunk path in CTS terms. `trunk` is preferable to `source_to_root` because it is a clock-network concept rather than a geometric endpoint phrase.
* `topology/buffer/BufferInsertion` replaces the old editor-style wording. It describes the CTS operation: inserting buffers and reconnecting temporary clock nets.
* `htree/` remains the concrete algorithm family and is promoted out of `topology/htree`, removing one path level.
* `htree/embedding` describes mapping a selected H-tree pattern into placed CTS objects. This is closer to physical-design/topology terminology than generic `construction`.
* H-tree internals can use shorter file/class names because folder and namespace meaning already provides the H-tree scope.

## Alternative Options

### Option A: `topology/` + `htree/` with CTS-friendly names

Shape:

```text
synthesis/topology/
  Topology.hh
  Topology.cc
  sink/
  trunk/
  buffer/
synthesis/htree/
  HTree.hh
  characterization/
  pattern/
  embedding/
```

Pros:

* Keeps exactly two synthesis subfolders for this area: `topology` and `htree`.
* Makes `topology` a real CTS topology formation boundary.
* Uses concise CTS terms instead of repeated `Clock`/`Tree`/`Synthesis` prefixes.
* Removes `transaction` and `editor` from the target architecture.

Cons:

* Requires moving more files than a pure H-tree promotion.
* Requires careful include/CMake/test migration because public names change.

Verdict:

* Recommended.

### Option B: Promote H-tree only, keep current topology file names

Shape:

```text
synthesis/topology/
  ClockSynthesis.hh
  ClockTreeSynthesisTransaction.hh
synthesis/htree/
  HTree.hh
  ...
```

Pros:

* Reduces H-tree depth with a smaller patch.
* Lower migration risk.

Cons:

* Leaves `topology` root with mixed responsibilities.
* Does not solve the empty `Topology.hh` problem.
* Keeps names such as `ClockTreeSynthesisTransaction`.

Verdict:

* Not recommended as the final architecture. It keeps the naming problem the user called out.

### Option C: Replace `topology` with `clock_tree`

Shape:

```text
synthesis/clock_tree/
synthesis/htree/
```

Pros:

* Directly solves both path-depth and readability issues.
* Moves current files according to actual responsibilities.
* Avoids a fake polymorphic `Topology` layer.

Cons:

* Repeats clock-tree meaning that is already implied by iCTS and `synthesis`.
* Encourages names like `ClockTreeSynthesis`, which add prefix/suffix noise.

Verdict:

* Rejected by latest naming feedback.

## Naming Notes

### Naming Principles

Use parent-folder meaning instead of repeating it in every file:

* Under `source/operation/iCTS`, do not repeat `CTS`.
* Under `flow/synthesis`, do not append `Synthesis` unless the class is the public `Synthesis` entry itself.
* Under `topology`, do not add arbitrary `Clock` or `Tree` prefixes.
* Under `htree`, do not prefix every internal helper with `HTree`; use a local namespace or folder scope.
* Use CTS/physical-design words for visible architecture: `topology`, `sink`, `branch`, `trunk`, `buffer`, `insertion`, `characterization`, `pattern`, `library`, `region`, `embedding`.
* Keep mechanical software words private or avoid them entirely in architecture names: `transaction`, `editor`, `manager`, `handler`, `processor`, `registry`, `profile`, `context`.

Recommended renames:

| Current | Proposed | Reason |
|---------|----------|--------|
| `topology/ClockSynthesis` | `topology/Topology` | This is the topology formation entry for one clock; `synthesis` is already the parent scope. |
| `topology/ClockSinkTreeSynthesizer` | `topology/sink/SinkBranch` | Builds sink-side branches; avoids redundant `Clock`/`Tree`/`Synthesis`. |
| `topology/ClockSourceRootSynthesizer` | `topology/trunk/SourceTrunk` | Builds the source trunk from clock source toward topology roots. |
| `topology/ClockTreeSynthesisTransaction` | fold into `topology/Topology` | Commit/rollback is an implementation safety detail, not a public architecture name. |
| `htree/construction/SourceToRootSegmentBuilder` | `topology/trunk/SourceTrunkSegment` or private helper in `SourceTrunk.cc` | The segment is part of the source trunk path, not H-tree-specific. |
| `htree/construction/ClockSynthesisNetEditor` | `topology/buffer/BufferInsertion` | Describes the CTS operation: buffer insertion and temporary net reconnection. |
| `htree/construction/HTreeClockTreeObjectBuilder` | `htree/embedding/Embedding` | Converts selected H-tree pattern into placed CTS objects; `embedding` is physical-design/topology language. |
| `htree/construction/HTreeClockTopologyBuildContext` | `htree/embedding/EmbeddingState` | Runtime state for H-tree embedding object names, counters, and temporary result placement. |
| `htree/characterization/ClockSynthesisHtreeOptions` | `htree/characterization/Options` | Parent directory already says H-tree characterization. |
| `htree/pattern_search/HTreePatternRegistry` | `htree/pattern/PatternLibrary` | EDA-friendly name for the segment/topology pattern collection indexed by `PatternId`; avoids software-mechanical `registry`. |
| `htree/pattern_search/HTreeSegmentCandidateFrontier` | `htree/pattern/SegmentFrontier` | Keeps the segment-pattern frontier meaning without repeating H-tree. |
| `htree/pattern_search/HTreeTopologyDepthSearch` | `htree/pattern/DepthSearch` | Search is over H-tree depth candidates inside the H-tree pattern module. |
| `htree/pattern_search/HTreeTopologyDepthEvaluation` | `htree/pattern/DepthEvaluation` | Evaluation is local to H-tree depth candidates. |
| `htree/pattern_search/HTreeSinkLoadProfile` | `htree/pattern/SinkLoadRegion` | Describes the sink-load capacitance/fanout legality region a selected pattern must cover; avoids generic `profile` and uncommon `envelope`. This is not a physical placement region. |

Names to avoid:

* `flow/synthesis/topology/htree` as the final path, because it keeps avoidable depth.
* `topology_selection`, because selection is too narrow and was already rejected.
* `assembly`, `planning`, `diagnostics`, because they are not CTS-business-readable enough here.
* `transaction`, because commit/rollback is an internal consistency mechanism, not a CTS architecture concept.
* `editor`, because the code is not an interactive editor; it performs buffer insertion and temporary net reconnection.
* `registry`, because the visible concept is a pattern library/table used by H-tree search, not a generic registration mechanism.
* `profile`, because the visible concept is a sink-load legality region or load bound checked during pattern legality.
* `context`, because visible files should state the CTS/H-tree state they carry, such as `EmbeddingState`.
* Repeated public prefixes such as `ClockTree...` and repeated suffixes such as `...Synthesis` below `flow/synthesis`, unless there is no clearer CTS noun.
* `writeback`, because the project has moved to `instantiation`/conversion terminology.

## Public Contract Cleanup

This专项 should also track a second-order issue: directory movement alone does not solve the size of the public structs.

Recommended follow-up after path cleanup:

* Split the current `ClockSynthesis::BuildResult` replacement under `topology/Topology` into:
  * stable result status
  * temporary construction artifacts
  * clustering summary
  * selected H-tree summary
* Split the current `HTreeBuilder::BuildResult` replacement under `htree/HTree` into:
  * selected H-tree topology result
  * search/characterization summary
  * construction artifacts
* Keep heavy candidate/search contracts private to `synthesis/htree/pattern_search`.
* High-level `Synthesis`, `distribution`, `trace`, `evaluation`, and `report` should not include H-tree candidate/search internals.

This can be a later phase. It should not block the directory flattening if the team wants a behavior-preserving path move first.

## Red Lines

Do not:

* Reintroduce `flow/htree` as a top-level peer beside `setup`, `synthesis`, `instantiation`, `evaluation`, and `report`.
* Keep an empty `Topology.hh` just to satisfy a directory name.
* Expose transaction/commit/rollback as visible architecture names.
* Put generic temporary object/net editing under H-tree if the code is used by non-H-tree source-trunk or clustering paths.
* Add compatibility forwarding headers for old include paths unless explicitly approved. The current user preference is no compatibility.
* Create a `topology` layer unless `Topology.hh/.cc` is the real public entry for CTS topology formation.

## Proposed Implementation Phases

### Phase 0: Analysis confirmation

Scope:

* Confirm `topology/` and `htree/` as the two final synthesis subfolders for this area.
* Confirm `SourceTrunk` as the source-to-root CTS term.

Acceptance:

* User approves one target architecture.
* PRD is updated with the chosen structure.

### Phase 1: Topology boundary consolidation

Scope:

* Turn `topology/Topology.hh/.cc` into the real primary entry.
* Move current `ClockSynthesis.*` behavior into `Topology`.
* Fold visible transaction naming into `Topology` as private commit/rollback helpers.
* Update includes, CMake, and tests.

Acceptance:

* `synthesis/topology` root contains only `Topology.hh`, `Topology.cc`, and build metadata.
* No visible source/test name contains `ClockTreeSynthesisTransaction`.
* Build target for `icts_source_flow_synthesis` passes.

### Phase 2: Sink branch and source trunk split

Scope:

* Move sink-side topology code to `synthesis/topology/sink/SinkBranch.*`.
* Move source-to-root topology code to `synthesis/topology/trunk/SourceTrunk.*`.
* Move source trunk segment code out of H-tree and into the trunk boundary.

Acceptance:

* Sink-side code no longer uses `ClockSinkTreeSynthesizer`.
* Source-side code no longer uses `ClockSourceRootSynthesizer` or `SourceToRootSegmentBuilder` as visible architecture names.

### Phase 3: H-tree promotion

Scope:

* Move `synthesis/topology/htree` to `synthesis/htree`.
* Rename local H-tree internals to use folder scope and shorter business terms where practical.
* Update includes, CMake target variables, test paths, and static old-path references.

Acceptance:

* No active source/test include path contains `synthesis/topology/htree`.
* H-tree CMake target remains hierarchical but no longer contains `topology` in the directory path.
* `synthesis/htree/HTree.hh` remains the family entry.

### Phase 4: Buffer insertion and H-tree embedding boundary

Scope:

* Move `ClockSynthesisNetEditor.*` to `synthesis/topology/buffer/BufferInsertion.*`.
* Move H-tree-specific object construction to `synthesis/htree/embedding/Embedding.*`.

Acceptance:

* No visible architecture name uses `Editor`.
* H-tree embedding contains only H-tree-specific mapping from selected pattern to placed CTS objects.
* Generic temporary buffer/net operations live under `topology/buffer`.

### Phase 5: Public contract narrowing

Scope:

* Split large build-result structs only where it reduces cross-module coupling.
* Keep behavior unchanged.

Acceptance:

* `Synthesis`, `distribution`, `trace`, `evaluation`, and `report` do not include H-tree candidate/search internals.
* Public records are stable boundary records or submodule-local contracts.

## Verification Plan For Later Implementation

Minimum non-binary checks after each phase:

```bash
git diff --check
cmake --build build --target icts_source_flow -j2
rg -n "synthesis/topology|flow/synthesis/topology|topology/htree" src/operation/iCTS/source src/operation/iCTS/test
```

Final checks if implementation is approved:

```bash
cmake --build build --target iEDA -j2
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```
