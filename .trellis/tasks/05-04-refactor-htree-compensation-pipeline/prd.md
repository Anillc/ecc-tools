# Refactor H-tree Compensation Pipeline

## Goal

Refactor the current H-tree root-driver compensation implementation so it is a first-class scoring stage in the H-tree candidate search pipeline instead of a post-filter patch. The compensation must be applied before final full-depth H-tree candidate frontier pruning, while keeping existing H-tree submodule semantics stable.

## Scope

Implement the previously agreed architecture cleanup in one task:

- Keep `HTree.cc` as the top-level orchestration entry.
- Keep `plan/DepthPlan` responsible for depth exploration and shared per-search pass lifetime.
- Keep `topology_pruning` responsible for candidate assembly, final compensated frontier construction, filtering, and selection.
- Keep `compensation` responsible for root-driver physical load resolution, direct Liberty lookup, caching, and applying delay/power compensation.
- Do not introduce temporary instrumentation or experiment-only production code.
- Do not update `.trellis/spec` in this task.
- Do not run `ecc_dev_tools` / ecc dev checks.

## Required Implementation

1. Apply root-driver compensation before final full-depth H-tree candidate frontier pruning.
   - Intermediate H-tree joins may keep existing raw state frontier pruning to control search size.
   - The last full-depth candidate set must be compensated first, then final state frontier pruning must consume compensated `get_delay()` / `get_power()`.
   - Remove later duplicate compensation over already-filtered candidate/feasible vectors.

2. Refactor compensation into a high-cohesion pass API.
   - Prefer a `RootDriverCompensationPass`-style API that owns options, stats, direct lookup cache, and root-load cache.
   - Hide cache keys and root-load implementation details from public headers where practical.
   - Keep the existing production behavior: root cell delay + internal/leakage cell power only; do not add root output net switching power.

3. Reduce coupling.
   - Remove `compensation -> embedding` dependency caused only by geometry interpolation.
   - Keep scoring/search modules independent from object-construction modules.

4. Preserve evaluation semantics.
   - Evaluation remains structured-metadata based for `h-tree root input pin -> h-tree leaf buffer output pin`.
   - Do not reintroduce name-pattern behavior such as `htree_edge_buf_` for decisions.

5. Keep source clean.
   - Remove or avoid experiment-only code.
   - Keep logs compact and production-facing.

## Validation

After implementation:

1. Build the changed source with a release binary.
2. Run the release iCTS dev binary/script and capture logs.
3. Build the pre-change git HEAD baseline in a separate clean worktree with release settings.
4. Run the same iCTS dev binary/script for the baseline and capture logs.
5. Compare from logs only:
   - selected optimal solution: depth, topology pattern id, level segment pattern ids;
   - H-tree metrics: raw char, compensation component, compensated metric, selected physical root load;
   - runtime change, especially HTree/build and overall flow runtime if available;
   - evaluation precision for `h-tree root input pin -> h-tree leaf buffer output pin`.

## Out of Scope

- ECC dev checks.
- Temporary source instrumentation.
- Direct-vs-char experiment probes.
- Full signoff STA/iPW equivalence.
- Large semantic changes to segment characterization or intermediate H-tree pruning.
