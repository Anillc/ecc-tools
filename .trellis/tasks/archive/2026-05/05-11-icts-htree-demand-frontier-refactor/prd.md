# refactor HTree demand-driven frontier synthesis

## Goal

Refactor the iCTS HTree segment frontier synthesis boundary so frontier construction is requested by consumer need instead of exposed through ad hoc "full" versus "all-only" entry points. The target is to make the current runtime optimization a clean demand-driven design, reduce accidental construction of unused semantic frontiers, and improve HTree readability without changing exact CTS search behavior unless explicitly validated and accepted.

## Background / Known Context

- The runtime optimization task introduced `SynthesizeSegmentAllFrontierEntrySets(...)` for `SourceTrunkSegment::build` because source-to-root top-segment synthesis only consumes `SegmentCandidateFrontierSet::all_frontier_entries`.
- The existing full API `SynthesizeSegmentEntrySets(...)` still materializes `all_frontier_entries`, `branch_buffered_entries`, and `leaf_unbuffered_entries`.
- Downstream HTree currently consumes `all_frontier_entries` by default and `branch_buffered_entries` when boundary constraints force terminal branch buffers. It has a `leaf_unbuffered` selection enum path, but no production caller currently selects that mode.
- The current public data shape leaks storage details to callers: `HTree.cc`, `TopologyPruning.cc`, and `SourceTrunkSegment.cc` all read `SegmentCandidateFrontierSet` fields directly.
- Segment frontier synthesis has semantic dependencies: every composed semantic frontier depends on upstream `all`, while downstream uses the requested semantic kind. For example, branch-frontier composition is `upstream.all + downstream.branch`.
- `HTree::build` already delegates some sub-areas, but it still owns a long orchestration path from input validation through topology generation, characterization, frontier synthesis, depth search, global selection, root-driver compensation, selected pattern decoration, embedding, and summary emission.
- The predecessor runtime-optimization task has been committed and archived. This refactor task now has a clean implementation boundary.

## Assumptions

- The first refactor slice should preserve default CTS QoR and selected physical topology. Internal pattern ID drift is allowed only if explicitly accepted and covered by stronger materialized-pattern equivalence checks.
- The new API should support exact eager construction by request first. A fully lazy cache can be a later extension if the request-based boundary proves stable.
- The implementation should stay inside `src/operation/iCTS/` and preserve the existing CTS flow architecture.
- Production logging should stay low-noise and use schema-backed helpers where new counters are needed.

## Frontier Types To Model

- `all`: the Pareto frontier over all segment candidates for a length. This is required by source trunk, unrestricted downstream HTree, and as the upstream dependency for every composed semantic frontier.
- `terminal_branch_buffered`: the frontier whose segment pattern has a terminal branch buffer. This is required by HTree when `force_branch_buffer` is active.
- `terminal_leaf_unbuffered`: the frontier whose segment pattern does not have a terminal branch buffer. This is currently modeled and composed, but no production downstream HTree path selects it today. It should remain supported as a first-class optional kind for diagnostics, equivalence tests, and future constraints.
- `full`: a request profile, not a storage type. It means `all | terminal_branch_buffered | terminal_leaf_unbuffered`.
- `consumer demand`: a request derived from caller semantics. Source trunk is `all`; normal HTree can be `all`; branch-forced HTree is `all | terminal_branch_buffered`; full diagnostic/equivalence mode is `full`.

## Requirements

- Introduce first-class request and catalog types for segment frontier synthesis, so callers ask for semantic frontier kinds instead of choosing between separate wrapper functions.
- Replace the current public `SynthesizeSegmentAllFrontierEntrySets(...)` patch API with a general request-based API.
- Keep existing exact full synthesis available through a request profile for regression and equivalence checks.
- Derive HTree frontier demand from boundary constraints instead of always assuming every semantic frontier is needed.
- Keep source trunk on the same request-based API as downstream HTree.
- Hide direct public access to `all_frontier_entries`, `branch_buffered_entries`, and `leaf_unbuffered_entries` behind typed accessors or a `SegmentFrontierCatalog`.
- Add tests that prove request masks build only requested semantic kinds while preserving the selected physical result for current supported flows.
- Record runtime and QoR comparisons for the `ics55_dev` target command when any default HTree demand is narrowed.
- Improve HTree module readability by extracting low-risk helpers around build orchestration and selected pattern decoration.

## Acceptance Criteria

- [ ] `prd.md`, `design.md`, `implement.md`, and a research note describe current frontier consumers, semantic types, and the decoupling plan.
- [ ] `SegmentPruning` exposes one request-based synthesis API and no source-trunk-specific all-only public wrapper remains.
- [ ] Source trunk and downstream HTree both consume segment frontiers through typed request/catalog APIs.
- [ ] If default downstream HTree demand is narrowed, before/after validation compares selected depth, materialized level buffer patterns, selected delay/power, root driver, final buffer count, wirelength, setup WNS, and hold WNS.
- [ ] Existing focused HTree tests pass, including branch-buffer coverage.
- [ ] A focused unit test covers at least `all`, `all | terminal_branch_buffered`, and `full` requests.
- [ ] Final validation includes `git diff --check`, affected iCTS test targets, `iEDA` build, and target benchmark if behavior-affecting defaults change. Per user instruction and iCTS project guidance, do not run `ecc_dev_tools` during the normal edit/build/test loop; reserve it for finish-work if requested.

## Out of Scope

- Approximate pruning, sampling, relaxed Pareto equivalence, or any QoR-changing search policy.
- CharBuilder characterization laziness or reachability optimization.
- Root-driver compensation algorithm changes.
- Large BuildResult ownership reshaping unless required by the request-based frontier refactor.
- Cross-module iDB/iSTA changes.

## Research References

- `research/htree-demand-frontier-refactor.md` - code-level analysis of current frontier types, consumers, coupling points, and refactor candidates.
- `.trellis/tasks/05-11-icts-runtime-optimization/` - predecessor runtime task that introduced the all-frontier-only fast path.
