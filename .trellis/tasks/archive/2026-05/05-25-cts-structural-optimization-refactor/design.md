# CTS structural optimization refactor design

## Status

`05-24-cts-contract-polish-convergence` is complete and archived. This task now starts from the post-convergence code shape.

The post-convergence audit confirms that the short-term contract cleanup succeeded at the public stage boundary: `Synthesis::run` takes
`SynthesisInput`, `Topology::formClock` takes `ClockTopologyInput`, and `HTreeSummary` no longer carries the large HTree diagnostic bundle through
topology summaries. The remaining work is structural readability and ownership cleanup.

## Starting Rationale

The desingleton refactor made ownership explicit. The contract-polish task finished the short-term boundary cleanup:

- named stage input contracts instead of public long parameter lists;
- CTS-level `SchemaWriter` spelling in business signatures;
- slim HTree/Topology summaries;
- report-only diagnostics no longer transported through production summaries.

After that, the remaining opportunity is structural: make CTS flow read like CTS business logic instead of a sequence of utility calls and transport
objects.

## Current Audit Findings

### Public Contracts Are Mostly Clean

- `src/operation/iCTS/source/flow/synthesis/Synthesis.hh` exposes `SynthesisInput`.
- `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh` exposes `ClockTopologyInput`, `TopologyInput`, `TopologyConfig`, and
  `SourceTrunkInput`.
- `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh` has separate `HTreeInput`, `HTreeConfig`, `HTreeOutput`, and `HTreeSummary`.
- `SchemaWriter` is already available as the CTS-level spelling in business signatures.

The next task is therefore not more mechanical wrapping. It is to remove the remaining production transport of data whose only consumers are HTree
reports, experiments, or tests.

### HTree Still Returns Diagnostics In The Production Build Type

`HTreeBuild` still contains:

- `HTreeOutput output`;
- `HTreeSummary summary`;
- `HTreeDiagnostics diagnostics`.

Production topology callers use only `summary` and `output`:

- `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc`;
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc`;
- `src/operation/iCTS/source/flow/synthesis/trace/topology_build/TopologyBuildTrace.cc`.

The diagnostic fields are consumed by HTree report code and tests:

- `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc`;
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeBuildObservation.hh`;
- real-tech HTree experiment and visualization helpers under `src/operation/iCTS/test/flow/synthesis/htree/`.

This is the clearest structural mismatch. The production build payload should not expose HTree diagnostic transport just because tests and report
helpers need it.

### HTree Build Has A Natural Short-Lived Owner

`src/operation/iCTS/source/flow/synthesis/htree/HTree.cc` is a long static facade method that coordinates root-net validation, topology generation,
characterization, depth search, analytical candidate selection, root-driver compensation, embedding, and summary/report emission.

The data only needs to live during one build. A per-build `HTreeBuilder` inside `HTree.cc` gives the build a clear owner without creating a flow-owned
service, singleton, or reusable state container.

### Topology Commit Boundary Is Explicit But Dense

`src/operation/iCTS/source/flow/synthesis/topology/Topology.cc` already separates:

- sink-domain build via `Topology::build`;
- source-trunk build via `Topology::buildSourceTrunk`;
- commit into `Design` via `DesignConversion::commitInsertedObjects`;
- reset on failure via `Topology::resetClockTopology`.

The risk is density rather than hidden state. `ClockTopologyFormation` still owns sink-domain synthesis, source-trunk synthesis, commit, layout merge,
domain-status reporting, and trace aggregation. This task should preserve the explicit commit boundary and only rename or split where it improves CTS
domain readability without broad behavior churn.

### Synthesis Is Clean At The Facade But Still Procedural Internally

`src/operation/iCTS/source/flow/synthesis/Synthesis.cc` has a clean public input contract, but its local `synthesizeClock(...)` still carries a long
dependency list and combines clock-source validation, sink-domain partition, sink-domain preparation, topology formation, per-clock layout merge, and
synthesis summary aggregation.

This can be improved after the HTree production/diagnostic boundary is fixed. The safe direction is a local per-clock run object, not a new global
context.

## Target Architecture

### Per-Clock Synthesis Pipeline

Current synthesis logic mixes clock iteration, sink-domain preparation, topology build, source-trunk build, layout accumulation, status reporting,
and commit handling.

Target direction:

```text
Synthesis
  -> ClockSynthesisRun
       -> SinkDomainSynthesis
       -> SourceTrunkSynthesis
       -> ClockTopologyCommitPlan
       -> ClockLayout / SynthesisTrace update
```

The point is not to add layers for their own sake. The point is to make each step answer a CTS-domain question:

- Which sink domains exist for this clock?
- Which topology payload was built for this sink domain?
- Which temporary CTS objects are ready to commit?
- What did we commit to `Design`?
- What flow-level summary should be aggregated?

### Build vs Commit Boundary

HTree/topology algorithms should build temporary payloads. Synthesis/instantiation should own commit decisions into `Design` and iDB.

Expected improvement:

- algorithm output owns temporary inst/pin/net payloads;
- commit plans make mutation explicit;
- failed builds destruct without changing final design;
- commit failure handling is localized and reviewable.

### HTree Build Object

Replace the large static HTree implementation body with a short-lived build object.

Target shape:

```cpp
class HTreeBuilder
{
 public:
  HTreeBuilder(const HTreeInput& input, const HTreeConfig& config);
  auto build() -> HTreeDiagnosticBuild;

 private:
  const HTreeInput& _input;
  const HTreeConfig& _config;
};
```

Constraints:

- The builder is per-build and short-lived.
- It must not become a flow-owned service or service locator.
- It should keep diagnostics local and emit reports while local candidate data is still valid.
- `HTree::build` returns production `HTreeBuild` with only `output` and `summary`.
- `HTree::buildWithDiagnostics` is the explicit test/experiment/report observation path.

### Diagnostics And Report Ownership

Report-only data should be emitted by the stage that owns the data. It should not be routed through upstream production summaries unless the
upstream caller needs it for control flow or aggregation.

Expected improvement:

- HTree report data is local to `HTreeBuilder` and HTree report helpers.
- Topology summary carries only topology-level status and aggregation fields.
- Tests validate report-only details through report artifacts or explicit test-only observation APIs.

### Typed Stage Policy

Global `Config` remains the parser/runtime configuration source, but lower stages should receive typed policy objects where it improves readability.

Expected improvement:

- Flow or synthesis derives `HTreeConfig`, `TopologyConfig`, `SourceTrunkConfig`, and characterization config once near the boundary.
- Lower modules do not repeatedly query broad runtime `Config` for unrelated fields.
- Config types stay minimal and contain only behavior-changing policy.

### Test Strategy

Tests should emphasize domain behavior:

- independent runtimes remain isolated;
- build payload and commit payload are inspectable at their natural boundary;
- reports/artifacts preserve diagnostic coverage;
- tests do not force production summaries to expose internal algorithm state.

## Non-Goals

- Do not reintroduce singletons, registries, global contexts, or broad runtime dependency objects.
- Do not rewrite CTS algorithms for QoR changes unless a bug is found.
- Do not move iDB/iSTA access out of `Wrapper` / `STAAdapter`.
- Do not redesign external `CTSAPI` signatures unless explicitly approved.
- Do not make HTree a long-lived topology/flow service. Its builder lifetime is one build call.
- Do not change report content intentionally in this task; report ownership and production payload shape are the target.

## Implementation Strategy

### Phase 1: HTree Production Payload vs Diagnostic Observation

1. Keep `HTreeInput`, `HTreeConfig`, `HTreeOutput`, and `HTreeSummary`.
2. Change production `HTreeBuild` to contain only `output` and `summary`.
3. Add `HTreeDiagnosticBuild`, extending the production build with `HTreeDiagnostics`.
4. Add `HTree::buildWithDiagnostics` for tests/experiments and HTree-owned report assembly.
5. Make topology production callers continue using `HTree::build`, so diagnostics stop crossing the topology boundary.
6. Update HTree internals that assemble reports or diagnostic counters to use `HTreeDiagnosticBuild`.
7. Update HTree tests and visualization helpers to call `buildWithDiagnostics` explicitly.

### Phase 2: Per-Clock Synthesis Readability

After Phase 1 is green, refactor `Synthesis.cc` locally:

- introduce a local per-clock run/counter structure if it removes the long `synthesizeClock(...)` parameter list;
- keep runtime dependencies passed from `SynthesisInput`, not through `CTSRuntime`;
- keep summary aggregation visible at the synthesis boundary.

### Phase 3: Topology Naming And Commit Review

After Phase 2 is green:

- review `ClockTopologyFormation` for narrow CTS-domain renames or helper extraction;
- preserve `DesignConversion::commitInsertedObjects` as the explicit commit boundary;
- avoid moving iDB or STA concerns into topology algorithms.

## Rollback Boundaries

- Phase 1 can be reverted independently because it changes HTree result ownership and HTree tests without changing topology QoR logic.
- Phase 2 is local to `Synthesis.cc` and should not change public contracts.
- Phase 3 is local to topology orchestration and must not change `DesignConversion` commit semantics.
