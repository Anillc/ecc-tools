# Refactor H-tree Compensation Pipeline

## Goal

Refactor the current H-tree root-driver compensation implementation to keep only one production semantics:
run the normal raw H-tree frontier search first, then apply root-driver compensation to the resulting H-tree frontier and select/rerank from that compensated frontier.

The production source must not keep the full raw-candidate compensated search path, streaming full-raw-candidate path, or support data structures that exist only for those discarded strategies.

## Scope

Implement the previously agreed architecture cleanup in one task:

- Keep `HTree.cc` as the top-level orchestration entry.
- Keep `plan/DepthPlan` responsible for depth exploration and shared per-search pass lifetime.
- Keep `topology_pruning` responsible for raw candidate assembly/frontier pruning, post-frontier compensation, filtering, and selection.
- Keep `compensation` responsible for root-driver physical load resolution, direct Liberty lookup, caching, and applying delay/power compensation.
- Do not introduce temporary instrumentation or experiment-only production code.
- Do not update `.trellis/spec` in this task unless a verification finding proves the new semantic contract must be recorded.
- Run the relevant `ecc_dev_tools` / ecc dev checks after implementation and converge them.

## Required Implementation

1. Keep only post-frontier root-driver compensation/reranking.
   - All H-tree joins, including the final full-depth join, use the existing raw H-tree state frontier pruning.
   - Do not materialize or stream the full final raw join output only for compensation.
   - Apply root-driver compensation to the raw H-tree frontier entries after raw frontier construction.
   - Selection, filtering, depth summaries, and global candidate pools must consume compensated `get_delay()` / `get_power()` after this post-frontier pass.
   - The report must describe the scope as compensation over the raw H-tree frontier, not full candidate-space compensated optimality.

2. Refactor compensation into a high-cohesion pass API.
   - Prefer a `RootDriverCompensationPass`-style API that owns options, stats, direct lookup cache, and root-load cache.
   - Hide cache keys and root-load implementation details from public headers where practical.
   - Keep the existing production behavior: root cell delay + internal/leakage cell power only; do not add root output net switching power.
   - Remove public or cross-module data structures that only supported the discarded full raw/streaming full-candidate compensation strategy.

3. Reduce coupling.
   - Remove `compensation -> embedding` dependency caused only by geometry interpolation.
   - Keep scoring/search modules independent from object-construction modules.

4. Preserve evaluation semantics.
   - Evaluation remains structured-metadata based for `h-tree root input pin -> h-tree leaf buffer output pin`.
   - Do not reintroduce name-pattern behavior such as `htree_edge_buf_` for decisions.

5. Keep source clean.
   - Remove or avoid experiment-only code.
   - Remove full-raw-candidate compensation support and related counts/storage if they are not part of the production post-frontier path.
   - Keep logs compact and production-facing.

## Validation

After implementation:

1. Build the changed source with a release binary.
2. Run relevant `ecc_dev_tools` / ecc dev checks and converge findings.
3. Run the release iCTS dev binary/script and capture logs.
4. Compare against the most recent pre-refactor run log when available:
   - selected optimal solution: depth, topology pattern id, level segment pattern ids;
   - H-tree metrics: raw char, compensation component, compensated metric, selected physical root load;
   - runtime change, especially HTree/build and overall flow runtime if available;
   - evaluation precision for `h-tree root input pin -> h-tree leaf buffer output pin`.

## Out of Scope

- Temporary source instrumentation.
- Direct-vs-char experiment probes.
- Full signoff STA/iPW equivalence.
- Full raw-candidate compensated pruning/search.
- Large semantic changes to segment characterization or raw H-tree pruning.
