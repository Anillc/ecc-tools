# CTS large header concept split

## Goal

Split broad CTS headers that act as semantic buckets into smaller CTS concept headers after the relevant source boundaries are clear.

## Parent Task

`.trellis/tasks/05-19-cts-code-normalization-refactor-research`

## Scope

Priority headers:

- `src/operation/iCTS/source/database/adapter/fast_sta/FastStaTypes.hh`
- `src/operation/iCTS/source/module/characterization/CharBuilder.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.hh`
- `src/operation/iCTS/source/flow/optimization/OptimizationTypes.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentLibrary.hh`

## Requirements

- Split by CTS concept, not by line count.
- Keep public facade headers small.
- Move private helper declarations into semantic internal headers with CTS-specific names.
- Use forward declarations where possible.
- Avoid vague bucket names such as `Types`, `Internal`, `Support`, `Input`, `Session`, or `Network`.

## Implementation Checklist

- [x] Confirm the stable concept boundaries for each header before editing.
- [x] Split `FastStaTypes.hh` after FastSTA submodule boundaries are clear.
- [x] Split `CharBuilder.hh` after characterization responsibilities are grouped.
- [x] Split `HTree.hh`, `OptimizationTypes.hh`, and `SegmentLibrary.hh` along stable CTS concepts.
- [x] Update includes and CMake.
- [x] Rebuild after each header split.

## Acceptance Criteria

- [x] Broad touched headers no longer group unrelated CTS concepts.
- [x] Public facade headers expose only required public API.
- [x] Private declarations live in CTS-semantic headers.
- [x] Affected targets build.

## Validation

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_module_characterization icts_source_flow_synthesis_htree icts_source_flow_optimization
```

Completed validation:

```bash
ninja -C build icts_source_module_characterization icts_source_flow_synthesis_htree_segment_pruning icts_source_flow_synthesis_htree_topology_pruning icts_source_flow_synthesis_htree_analytical_solver icts_source_flow_synthesis_htree icts_test_flow_synthesis_htree_analytical_solver
ctest --test-dir build -R '^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure
ninja -C build icts_source_database_adapter_fast_sta icts_source_module_characterization icts_source_flow_synthesis_htree icts_source_flow_optimization
git diff --check
```

Source naming/include scans:

```bash
rg -n "SegmentFrontierRequest|MakeHTreeSegmentFrontierRequest|\bRequest\b|Response|Snapshot|Types|Internal|Session|Network|Fallback|Rollback|fallback|rollback|ResolvedModelInputs|ResolveModelInputs|ResolveModelInputRootSlewNs" src/operation/iCTS/source/module/characterization src/operation/iCTS/source/flow/synthesis/htree src/operation/iCTS/source/flow/synthesis/topology/trunk -g '*.cc' -g '*.hh'
find src/operation/iCTS/source/module/characterization src/operation/iCTS/source/flow/synthesis/htree src/operation/iCTS/source/flow/synthesis/topology/trunk -name '*Internal*' -o -name '*Support*' -o -name '*Request*' -o -name '*Response*' -o -name '*Snapshot*' -o -name '*Types*' -o -name '*Input*' -o -name '*Session*' -o -name '*Network*' -o -name '*Fallback*' -o -name '*Rollback*'
rg -n "SegmentLibrary.hh" src/operation/iCTS/source src/operation/iCTS/test -g '*.cc' -g '*.hh'
```

Scan results:

- No source identifier/file matches for the banned generic structural terms listed above.
- `SegmentLibrary.hh` remains only as the compatibility include itself; source and test consumers now include the concept headers directly.

Completed source splits:

- `CharBuilder.hh` now keeps the public characterization builder facade, with `CharacterizationBufferCell.hh` and `CharBuilderSweepState.hh` carrying buffer-cell data and private sweep/build state.
- `HTree.hh` now keeps the H-tree synthesis entry and compatibility aliases; `HTreeSynthesisOptions.hh` and `HTreeSynthesisResult.hh` carry synthesis option/result contracts.
- `SegmentLibrary.hh` is now a compatibility include; segment frontier, segment pattern, and topology pattern contracts live in `SegmentFrontierCatalog.hh`, `SegmentPatternLibrary.hh`, and `TopologyPatternLibrary.hh`.
- `OptimizationTypes.hh` and `FastStaTypes.hh` had already been removed by earlier child work in this campaign.
