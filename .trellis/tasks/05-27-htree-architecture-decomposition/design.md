# Design: H-tree architecture decomposition

## Current Structure

The current H-tree build path is improved from the previous state but still has
one large orchestration method in `HTree.cc`.

The non-trivial path is:

```text
HTreeBuilder::build
  -> validate required dependencies
  -> begin HTree/build stage
  -> TopologyGen::build
  -> RunCharacterizationFlow
  -> ResolveBoundaryConstraints
  -> ResolvePatternSearchBoundaryConstraints
  -> BuildLevelPlans
  -> ResolveDepthCandidates
  -> build BufferPatternLibrary
  -> maybe synthesize SegmentFrontierCatalog for discrete
  -> build RootDriverCompensationInput
  -> build HTreeFanoutPruningConfig
  -> build SinkLoadRegionLegalityInput
  -> SelectAnalyticalHTreeSolution or SelectDiscreteHTreeSolution
  -> FinalizeSelectedHTreeSolution
```

The current shape has useful boundaries:

- `characterization/Characterization.*` owns characterization grid adaptation
  and `CharBuilder` setup.
- `solution/discrete/DiscreteSolution.*` owns discrete candidate search and
  selected candidate construction.
- `solution/analytical/AnalyticalSolution.*` owns analytical candidate solve and
  selected candidate construction.
- `solution/finalization/SolutionFinalizer.*` owns shared final materialization,
  root-driver sizing, embedding, and final reports.

The missing boundary is the shared synthesis-state contract that all selection
engines consume.

## Main Problems

1. `HTree.cc` still owns too many H-tree internals.
2. Discrete and analytical selector signatures duplicate nearly the same long
   dependency list.
3. The build state is implicit: `DiagnosticBuild`, topology, characterization
   facts, boundary constraints, level plans, pattern libraries, root-driver
   inputs, and sink-load legality inputs are not grouped as one stage contract.
4. It is hard to add another selection engine or compare engines because callers
   must understand how every shared synthesis value is assembled.
5. `StageScope` and local `CharacterizationLibrary` lifetime details are subtle
   enough that they should be made explicit in design rather than left as local
   method structure.

## Recommended Architecture

Keep `HTree` as the public flow boundary. Add a shared H-tree synthesis-state
layer under `source/flow/synthesis/htree`, then make `HTree.cc` a small
orchestrator.

User-approved naming:

```text
src/operation/iCTS/source/flow/synthesis/htree/
  synthesis_state/
    SynthesisState.hh
    SynthesisState.cc
```

The new target should be linked by:

- `icts_source_flow_synthesis_htree`
- `icts_source_flow_synthesis_htree_solution`

The state-construction target depends on existing H-tree submodules such as
characterization, constraint, plan, segment_pruning, compensation, region, and
the topology module. It must not depend on `solution` to avoid a cycle.

## Synthesis-State Contract

Use a module-qualified contract, not a generic global context. Suggested names:

```cpp
namespace icts::htree {

enum class HTreeSynthesisStateStatus
{
  kReady,
  kCompleted,
  kFailed,
};

struct HTreeSynthesisState
{
  const HTree::Input* input = nullptr;
  const HTree::Config* config = nullptr;
  DiagnosticBuild result;

  std::optional<CharacterizationLibrary> local_char_library = std::nullopt;
  double char_length_step_um = 0.0;

  BoundaryConstraints base_boundary_constraints;
  BoundaryConstraints search_boundary_constraints;
  std::vector<HTree::LevelPlan> full_level_plans;
  std::vector<unsigned> depth_candidates;
  unsigned max_depth = 0U;

  std::optional<BufferPatternLibrary> segment_pattern_library = std::nullopt;
  HTreeFanoutPruningConfig fanout_pruning_config;
  RootDriverCompensationInput root_driver_compensation_input;
  SinkLoadRegionLegalityInput sink_load_region_input;
  std::string root_driver_clock_period_source;
  bool strict_root_boundary_closure = false;

  auto charLibrary() -> CharacterizationLibrary&;
  auto charBuilder() const -> const CharBuilder&;
  auto segmentPatterns() -> BufferPatternLibrary&;
};

struct HTreeSynthesisStateBuild
{
  HTreeSynthesisStateStatus status = HTreeSynthesisStateStatus::kFailed;
  std::string failure_reason;
  HTreeSynthesisState state;
};

auto AssembleHTreeSynthesisState(const HTree::Input& input, const HTree::Config& config,
                                 SchemaWriter::StageScope& build_stage) -> HTreeSynthesisStateBuild;

}  // namespace icts::htree
```

Key lifetime rules:

- Do not store a raw `CharacterizationLibrary*` that may point into a movable
  local optional. Use `input.characterization_library` if present; otherwise use
  `local_char_library` through an accessor.
- Store `BufferPatternLibrary` in `std::optional` because it requires
  `STAAdapter&` at construction.
- Do not store `SchemaWriter::StageScope` inside the synthesis state. Keep the
  build stage in `HTree.cc` and pass it by reference into state construction and
  finalization.
- Let selectors mutate `state.result.diagnostics` when they already do so
  today; this preserves current diagnostic flow while reducing argument lists.
- Do not include `SegmentFrontierCatalog` in the common state. Under the
  selected Option B split, discrete selector owns required segment-frontier
  resolution and synthesis.

## HTree Orchestration After Refactor

Target shape:

```cpp
auto HTreeBuilder::build() -> htree::DiagnosticBuild
{
  auto initial = htree::HandleHTreeTrivialBuild(input, config);
  if (initial.status != kNeedsNonTrivialBuild) {
    return std::move(initial.result);
  }

  auto build_stage = reporter.beginStage(...);
  auto state_build = htree::AssembleHTreeSynthesisState(input, config, build_stage);
  if (state_build.status != HTreeSynthesisStateStatus::kReady) {
    return std::move(state_build.state.result);
  }

  auto selection = htree::SelectHTreeSolution(state_build.state);
  if (!selection.selected) {
    state_build.state.result.summary.failure_reason = selection.failure_reason;
    build_stage.failed(...);
    return std::move(state_build.state.result);
  }

  htree::FinalizeSelectedHTreeSolution(state_build.state, build_stage, selection.selected_solution);
  return std::move(state_build.state.result);
}
```

The exact function names can be adjusted during implementation, but this is the
desired dependency shape.

## Selection Contract Cleanup

Current duplicate wrappers:

```cpp
DiscreteHTreeSelectionBuild
AnalyticalHTreeSelectionBuild
```

Recommended replacement:

```cpp
struct HTreeSelectionBuild
{
  bool selected = false;
  std::string failure_reason;
  HTreeSelectionEngine engine = HTreeSelectionEngine::kDiscrete;
  HTreeSelectedSolution selected_solution;
};
```

This can live near `HTreeSelectedSolution`, either in
`solution/finalization/SolutionFinalizer.hh` for a minimal diff or in a new
small selected-solution contract header if the implementation benefits from
separating selection result from finalization.

Update selector entry points to:

```cpp
auto SelectDiscreteHTreeSolution(HTreeSynthesisState& state) -> HTreeSelectionBuild;
auto SelectAnalyticalHTreeSolution(HTreeSynthesisState& state) -> HTreeSelectionBuild;
```

Then optionally add a dispatch helper:

```cpp
auto SelectHTreeSolution(HTreeSynthesisState& state) -> HTreeSelectionBuild;
```

`SelectHTreeSolution` should do only runtime dispatch and failure
normalization. It should not know algorithm internals.

## Segment Frontier Ownership

Two viable placements:

### Option A: Shared State Owns Segment Frontiers

State construction synthesizes `SegmentFrontierCatalog` only when analytical is
disabled. Analytical state construction emits the same skipped report currently
emitted by `HTree.cc`.

Pros:
- smallest selector change;
- preserves current report stage ordering;
- shared state contains every prepared object needed by selection.

Cons:
- shared state construction contains one branch that exists only for discrete.

### Option B: Discrete Selector Owns Segment Frontiers

State construction builds only shared data. Discrete selector resolves required
frontiers and synthesizes `SegmentFrontierCatalog` internally.

Pros:
- cleaner shared-state semantics;
- discrete-only work stays in discrete selector.

Cons:
- report ordering changes slightly unless carefully preserved;
- discrete selector gets more responsibility in the first refactor.

User decision: use Option B. This is the stronger architectural split because
`SegmentFrontierCatalog` is a discrete-only search artifact and should not be
part of the shared synthesis state. Preserve the current report stage name
`HTree/Synthesize segment frontiers`; it will be emitted by the discrete
selector. Analytical selector should not emit a fake segment-frontier skip unless
review shows that report continuity is more important than algorithm ownership.

## Finalizer Cleanup

Current finalizer signature:

```cpp
FinalizeSelectedHTreeSolution(DiagnosticBuild& result, const HTree::Input& input,
                              const HTree::Config& config, StageScope& build_stage,
                              const HTreeSelectedSolution& selected_solution,
                              BufferPatternLibrary& segment_pattern_library)
```

Recommended signature after state extraction:

```cpp
FinalizeSelectedHTreeSolution(HTreeSynthesisState& state,
                              SchemaWriter::StageScope& build_stage,
                              const HTreeSelectedSolution& selected_solution)
```

This removes another repeated argument list and makes finalization consume the
same contract as selectors.

## Compatibility

- `HTree::build` remains the public entry point.
- Discrete remains default.
- Analytical remains controlled by `Config::enable_analytical_solver`.
- Reports should keep the same stage names where practical:
  - `HTree/build`
  - `HTree/Synthesize segment frontiers`
  - `HTree/Search topology depth candidates`
  - `HTree/Filter global sink-load coverage`
  - `HTree/Select global topology`
  - `HTree/Select analytical topology candidates`
  - `HTree/Build selected embedding`
  - `HTree/Emit synthesis summary`
- The refactor should not change selected pattern semantics, root-driver
  compensation, sink-load legality, or embedding.

## Trade-offs

- A full generic pipeline framework is not recommended. The H-tree flow is
  domain-specific and should stay inside `source/flow/synthesis/htree`.
- A class-based pipeline could manage lifetimes, but a move-only
  `HTreeSynthesisState` plus free state-construction/selection/finalization
  functions matches the existing functional style better.
- Moving segment frontier synthesis into discrete selector is architecturally
  cleaner and is now the selected direction, with real-design validation used to
  catch any behavior/report drift.

## Risks

- State move semantics can invalidate internal raw pointers if implemented
  carelessly. Avoid self-pointing fields.
- `SchemaWriter::StageScope` lifetime must remain clear; do not hide it in
  optional synthesis-state fields unless necessary.
- `CharacterizationLibrary` reuse vs local ownership must remain behaviorally
  identical.
- `BufferPatternLibrary::retainOnly` mutates pattern availability; state
  and selector ownership must preserve current mutation order.
- Any report-stage name/order changes may affect tests or downstream debugging
  even if QoR remains unchanged.

## Recommended MVP

Implement in one task:

1. Add `synthesis_state/SynthesisState.hh/.cc` and CMake target.
2. Add `HTreeSynthesisState` and `AssembleHTreeSynthesisState`.
3. Update selectors and finalizer to consume the state.
4. Add common `HTreeSelectionBuild`.
5. Move segment-frontier synthesis into the discrete selector.
6. Keep all algorithm math/search behavior unchanged.
7. Run focused tests plus `ics55_dev` and `ics55_huge_dev`.

Do not split this into smaller child tasks unless the first implementation
reveals unexpected CMake or lifetime complexity.
