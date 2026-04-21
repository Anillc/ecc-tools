# Char / H-Tree Refactor Inventory

## Purpose

This document records the concrete code that is still wrong, stale, redundant, or no longer necessary if the task scope is fixed to the agreed five semantic issues:

1. one explicit lattice semantics for `length` / `slew` / `load`
2. reusable shortest closure for required-length synthesis
3. characterization power excludes source/sink fixture cells
4. frontier-only composition
5. unified `group` / `frontier` / `compose` semantics across segment and H-tree

The goal is to make the mainline implementation and its supporting tests/reporting speak one consistent semantics, instead of keeping compatibility scaffolding that encodes the legacy behavior.

## Mainline Refactor Targets

### A. Legacy relaxed composition still exists in real-tech characterization test support

Files:
- `src/operation/iCTS/test/module/characterization/CharacterizationRealTechSmokeTest.cc`
- `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.hh`
- `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc`

Problems:
- `CharacterizationRealTechSmokeTest` still falls back from `exact_compose` to `relaxed_compose`.
- test support still exposes `ComposeSegmentEntriesRelaxed(...)`
- test support still exposes `SynthesizeSegmentFrontierIfMissing(...)`
- test support still caps candidates per boundary group before compose

Why this is wrong now:
- The frozen task semantics say compose must be frontier-only and exact.
- The frozen task semantics also say group/frontier/compose must be unified across segment and H-tree.
- Keeping a relaxed fallback in tests preserves the old algorithm contract and can make broken mainline semantics look acceptable.

### B. Test-only frontier grouping still uses the old boundary definition

Files:
- `src/operation/iCTS/source/module/characterization/Pruner.hh`
- `src/operation/iCTS/test/module/characterization/PrunerTest.cc`
- `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc`

Problems:
- `InputBoundaryPruner` only groups by `(input_slew_idx, driven_cap_idx)`.
- `ParetoPruner` still groups by `pattern_id`.
- real-tech test support builds frontiers with the old pruners instead of the new state-aware frontier helpers.

Why this is wrong now:
- The new `group` semantics must include compose-visible boundary state and terminal semantics, not just input electrical boundaries and not pattern identity.
- Old grouping rules can merge entries that must remain distinct under exact compose legality.

### C. H-tree result/report naming still describes old “pattern representative” semantics

Files:
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh`
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc`
- `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.cc`
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc`

Problems:
- result fields are still named `candidate_pattern_representatives` / `feasible_pattern_representatives`
- reports still emit `candidate_pattern_representative_count` / `feasible_pattern_representative_count`
- visualization still labels points as `pattern representative`
- one report path still emits the old `pattern_worst_case_pareto_power_median` selection-policy name

Why this is wrong now:
- these entries are now actual-load-legal frontier entries, not structural pattern representatives
- stale names make later analysis misleading even if the mainline logic is already correct

### D. Boundary-index names still carry the old floor/discretize language

Files:
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh`
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc`

Problems:
- public/internal fields still use `top_input_slew_floor_idx`
- public/internal fields still use `leaf_driven_cap_floor_idx`

Why this is wrong now:
- the new lattice contract is based on explicit covering-bin semantics
- keeping `floor` in the API leaks the old mixed discretization mental model back into the code

### E. Dead or misleading H-tree bookkeeping remains in the mainline flow

Files:
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh`
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc`

Problems:
- `CandidateBuildEvaluation` still carries per-candidate boundary-fallback fields that are never consumed by the final selection flow
- `BuildResult::failure_level` / `failure_length_idx` are exposed but not meaningfully propagated to the final result

Why this matters:
- unused bookkeeping increases maintenance noise
- public fields that are not actually maintained create a false debugging contract

## Secondary Legacy / Compatibility Debt

### F. `relaxed_candidates_per_boundary_group` is now a legacy config knob

Files:
- `src/operation/iCTS/source/database/config/Config.hh`
- `src/operation/iCTS/source/database/config/Config.cc`

Problem:
- the knob still exists in runtime config even though the active mainline flow no longer uses relaxed composition

Why this is legacy:
- today it only serves the old test-support path
- keeping it in the active runtime-config surface suggests it still changes the real algorithm

### G. `max_length` remains only as a compatibility placeholder

Files:
- `src/operation/iCTS/source/database/config/Config.hh`
- `src/operation/iCTS/source/database/config/Config.cc`

Problem:
- `max_length` is still parseable and reported even though the active length lattice is `wire_length_unit_um + wire_length_iterations`

Why this is legacy:
- it does not define the active discretized length semantics anymore
- if kept, it must stay clearly marked as compatibility-only

## Refactor Policy

Mainline cleanup should prioritize:

1. remove or rewrite code paths that still encode relaxed / legacy semantics
2. rename public and reporting interfaces so they describe the actual semantics
3. delete dead bookkeeping where it is safe
4. keep compatibility-only config only if removing it would create unnecessary churn outside the frozen five-issue scope

## Validation After Refactor

After refactoring the items above:

- rerun the ARM9 full-sink H-tree experiment matrix
- rerun an analogous full-sink real-tech ClockSynthesis validation
- report the two result sets separately
