# CTS structural optimization post-convergence audit

Date: 2026-05-25

## Scope

Audited the current post-convergence iCTS code for the active structural optimization task:

- `src/operation/iCTS/source/flow/synthesis/Synthesis.hh`
- `src/operation/iCTS/source/flow/synthesis/Synthesis.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh`
- `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- HTree submodules under `src/operation/iCTS/source/flow/synthesis/htree/`
- HTree tests and experiment helpers under `src/operation/iCTS/test/flow/synthesis/htree/`

## Confirmed cleanups from short-term task

- `Synthesis::run` uses `SynthesisInput`.
- `Topology::formClock` uses `ClockTopologyInput`.
- `Topology::build` uses `TopologyInput` and `TopologyConfig`.
- `Topology::buildSourceTrunk` uses `SourceTrunkInput`.
- Topology and source-trunk summaries no longer embed the full HTree summary or diagnostic bundle.
- Production topology callers consume HTree output and caller-relevant summary fields only.

## Structural hotspots

### HTree diagnostic ownership

`HTreeBuild` still contains `HTreeDiagnostics`. Production topology code does not need it:

- `SinkBranch.cc` calls `HTree::build` and passes the build into `RecordSinkHtreeBuild`, which transfers payload ownership and reads only
  output/summary fields.
- `SourceTrunk.cc` calls `HTree::build` and passes the build into `RecordTopHtreeBuild`, which also reads only output/summary fields.
- `TopologyBuildTrace.cc` stores HTree topology payload and inserted object ownership; it does not need diagnostic counters.

Diagnostics are used by:

- HTree report assembly in `solution/report/SolutionReport.cc`;
- HTree internal build helpers such as characterization, analytical selection, root-driver compensation, and embedding;
- HTree tests and experiment artifacts.

Action: split production `HTreeBuild` from explicit diagnostic observation.

### HTree build lifetime

`HTree::build` is a single static implementation body with a natural one-build lifetime. The method owns temporary topology, selected candidates,
diagnostics, embedding payload, and report emission. This should become an internal per-build object. The builder must not escape the call.

Action: introduce an internal `HTreeBuilder` in `HTree.cc`. Public production callers still get only `HTreeBuild`.

### Synthesis per-clock orchestration

`Synthesis.hh` is clean, but `Synthesis.cc::synthesizeClock` still has a long parameter list and combines validation, sink-domain partition,
topology formation, layout merge, and summary aggregation.

Action after HTree split: consider a local per-clock run object to make the synthesis stage read as clock validation -> sink-domain preparation ->
clock topology formation -> layout merge -> summary aggregation.

### Topology commit boundary

`Topology.cc` already has explicit commit stages for sink-domain and source-trunk builds. The density is high, but behavior is clear enough to avoid
a risky broad rewrite in the first phase.

Action after HTree split: review names and helper boundaries; preserve the explicit `DesignConversion::commitInsertedObjects` mutation point.

## Initial validation targets

```bash
ninja -C build icts_source_flow icts_test_flow_synthesis_htree icts_test_flow_synthesis
ctest --test-dir build -R '^icts_test_' --output-on-failure
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```
