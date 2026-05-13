# H-tree Code Map

## Primary Source Files

- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
  - Orchestrates the H-tree build flow.
  - Builds topology, characterization flow options, level plans, segment frontier request/catalog, candidate depth exploration, global candidate selection, and final embedding.
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
  - Composes segment frontiers into H-tree topology frontiers.
  - Evaluates depth candidates, applies boundary constraints, sink-load-region filtering, compensation, and selected candidate ranking.
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentPruning.cc`
  - Builds and prunes segment characterization frontiers.
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentLibrary.hh`
  - Stores segment/topology pattern metadata and composition states used during pruning and materialization.
- `src/operation/iCTS/source/flow/synthesis/htree/characterization/Characterization.cc`
  - Creates characterization entries from configured wirelength/slew/cap grids.
- `src/operation/iCTS/source/flow/synthesis/htree/embedding/Embedding.cc`
  - Materializes the selected H-tree topology into CTS buffers/nets.
- `src/operation/iCTS/source/database/config/Config.cc`
  - Parses `wirelength_iterations`, `slew_steps`, and `cap_steps` from CTS JSON.

## Existing Sweep Test

- `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechMatrixSupport.cc`
  - Existing matrix runner.
  - Hard-coded matrix is currently `{2, 3, 4, 5}` iterations and `{10, 15}` steps via constants in `HTreeRealTechSmokeSupport.hh`.
  - Uses test real-tech setup and selected largest real clock loads, not explicitly the `scripts/design/ics55_dev/run_iCTS_dev.tcl` executable flow requested for this task.

## Algorithm Shape

1. `HTree::build()` reads CTS config and initializes a characterization flow.
2. A topology is built from the root net and sink loads.
3. `BuildLevelPlans()` maps topology levels to aligned characterization length indices.
4. `BuildSegmentFrontierRequest()` determines which segment length/kind frontiers are required.
5. `SynthesizeSegmentFrontiers()` builds demand-driven segment frontiers for only required lengths/kinds.
6. `ExploreCandidateBuilds()` tries candidate H-tree depths.
7. For each candidate, `BuildPatternSearch()`:
   - Seeds the first level from matching segment frontier entries.
   - Repeatedly composes current H-tree frontier with the next level's segment seed entries.
   - Prunes frontier entries by state/Pareto dominance.
   - Applies root-driver compensation and boundary/sink-load legality filtering.
8. A global candidate is selected from feasible or fallback pools.
9. `BuildEmbedding()` materializes the selected pattern.

## Likely Performance-Sensitive Areas

- Segment frontier synthesis over larger length/slew/cap grids.
- Hash-join composition in `ComposeHTreeFrontierEntries()`.
- Repeated candidate-depth evaluation and sink-load-region legality filtering.
- Pattern metadata/library materialization and composition-state cache growth.
- Root-driver compensation over large frontiers.
