# Technical Design

## Objective

Optimize the measured runtime bottleneck for the `ics55_dev` iCTS relaxed-slew benchmark while preserving the current exact CTS result by default.

## Baseline Boundary

The current production baseline is treated as the opt3 H-tree state described by archived rollback notes:

- exact Pareto sort/scan is already present;
- per-depth Pareto compression is already present;
- root-load signature cache is already present;
- later post-opt3 P1/P2/P3/P4/P6 code should not be reintroduced without new evidence.

## Runtime Attribution Plan

1. Run a fresh baseline for the target command and copy generated logs into task artifacts. (Done)
2. Parse top-level CTS runtime from `cts.log`. (Done)
3. Use schema stage markers and source inspection to break down `synthesis`, especially:
   - `CharBuilder::build`;
   - `HTree::build`;
   - segment entry synthesis;
   - topology pattern search and depth candidate evaluation;
   - root-driver compensation;
   - sink-load-region / coverage filtering;
   - embedding and result construction.
4. Existing logs do not expose sufficient detail for the next optimization. Add production-quality scoped runtime metrics at synthesis/HTree substage boundaries. Runtime logs must use existing `SCHEMA_WRITER_INST` helpers and `LOG_*` rules.

## Primary Optimization Hypothesis

The first implementation candidate should be a measurement-enabling optimization slice: add substage runtime metrics and, if attribution confirms characterization remains a major actionable component, add default-off characterization reachability accounting. Current evidence points to characterization-side reachability/laziness as the most promising materially different direction because prior late-frontier pruning attempts either occurred after expensive work or lacked exactness proof.

Candidate A: characterization reachability analyzer before lazy characterization.

- Add default-off counters around characterized tuples and tuple consumption by segment synthesis / final topology.
- Determine whether a material fraction of `length, pattern, input_slew, load_cap` samples is never used by exact downstream search.
- No default behavior change; low QoR risk.
- If opportunity is material, follow with exact lazy characterization in a later implementation slice.

Candidate B: production HTree substage runtime metrics.

- Add low-noise substage runtime tables around HTree internals.
- Improves future profiling and task confidence.
- Does not directly reduce runtime, but fresh attribution is still ambiguous enough that this is the recommended first slice.

Candidate C: exact compose-time pruning with proof harness.

- Attack frontier generation before large materialization.
- Must compare exact frontiers, global Pareto set, selected median, selected topology ids, and fallback behavior.
- Higher potential runtime benefit, but prior attempts show higher proof risk.

## Implemented Optimization

Substage metrics showed that the largest previously hidden post-HTree synthesis cost was source-to-root top-segment frontier synthesis, not sink-domain commit/layout. `SourceTrunkSegment::build` only consumes `SegmentCandidateFrontierSet::all_frontier_entries`, while the shared HTree segment synthesis API also materialized `branch_buffered_entries` and `leaf_unbuffered_entries`.

The implemented optimization adds `SynthesizeSegmentAllFrontierEntrySets(...)` beside the existing full `SynthesizeSegmentEntrySets(...)`:

- downstream HTree keeps the full all/branch/leaf frontier synthesis path;
- source-to-root top-segment synthesis uses the all-frontier-only path;
- no CTS config or default search policy changes are introduced;
- source-trunk internal pattern ids may differ because unused branch/leaf pattern ids are no longer allocated, but committed CTS topology/QoR must remain unchanged.

Measured result on `ics55_dev`: process wall time improved from `69.99 s` fresh baseline to `65.32 s`, CTS synthesis improved from `31.167 s` to `27.603 s`, and source-trunk build improved from `10.181 s` pre-opt instrumentation to `5.231 s`.

## Correctness Contract

For default production behavior, before/after runs must preserve:

- selected depth;
- selected topology pattern id;
- selected level segment pattern ids;
- selected delay / power and raw H-tree metric;
- selected physical root load and root driver cell;
- final clock buffer count;
- total clock network wirelength;
- setup WNS and hold WNS within normal reporting precision;
- no new CTS fatal/warning behavior for the target command.

Any approximate optimization must be explicitly marked as non-default or require user approval before implementation.

## Data Flow / Boundaries

Relevant flow path:

```text
Tcl run_cts -> CtsIO::runCTS -> CTSAPI::init/runCTS -> Flow::runCTS
  -> Setup -> read_data -> Synthesis -> Topology / HTree / CharBuilder
  -> Instantiation -> Evaluation -> Report
```

Likely code boundaries:

- `src/operation/iCTS/source/flow/synthesis/Synthesis.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkLoadClustering.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunkSegment.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/characterization/Characterization.cc`
- `src/operation/iCTS/source/module/characterization/CharBuilder*.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/`
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/`
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/`
- `src/operation/iCTS/source/flow/synthesis/htree/region/`

Module code must receive explicit options/config and should not add new singleton access. External iDB/iSTA access must remain inside existing wrapper/adapter boundaries.

## Rollout / Rollback

- Keep measurement artifacts under the task directory.
- Keep temporary instrumentation either reverted before finish or converted to low-noise production logging through schema helpers.
- Use `git diff --check`, focused HTree/flow tests, iEDA build, and benchmark comparison as rollback gates.
- If an optimization changes selected topology or QoR unexpectedly, stop and either revert or document the hypothesis as falsified.
