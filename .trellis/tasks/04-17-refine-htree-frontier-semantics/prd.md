# Refine iCTS H-Tree Frontier Semantics

## Goal
Refine iCTS characterization and H-tree composition so frontier pruning preserves future exact-join capability, keeps terminal-semantic families separated, and retains the raw-solution capability needed to build `leaf_unbuffered` H-tree patterns.

## Validated Problems
- Current frontier pruning groups by input boundary and compares output/load/delay/power, but later exact join depends on exposed downstream electrical boundary:
  - `SegmentTraits`: exact join probes by `(output_slew_idx, load_cap_idx)`
  - `HTreeTraits`: exact join probes by `(output_slew_idx, half(load_cap_idx))`
- Current segment frontier synthesis only keeps frontier-to-frontier results for `all_entries` and `branch_entries`, which cannot represent:
  - raw upstream + frontier downstream expansion
  - final-stage raw upstream + `leaf_unbuffered` downstream expansion
- Current naming/semantics drift obscures the actual meanings of maintained entry sets and makes leaf-terminal semantics incomplete.

## Current Mainline Hypotheses
- Hypothesis 1: the current real-tech `max_slew` / `max_cap` bounds are too tight for `leaf_unbuffered`, so many propagated `output_slew_idx` / `driven_cap_idx` states overflow beyond the intended sweep lattice and the parent-connectable boundary coverage collapses.
- Hypothesis 2: the current `slew_steps` / `cap_steps` resolution is too coarse, so boundary quantization is overly lossy and squeezes the exact/relaxed join domain even when the physical states are near-connectable.
- Current focus is to validate these two hypotheses with real-tech experiments before changing the frontier/semantic logic again.

## Requirements
- Replace the over-aggressive frontier pruning basis with future join capability + terminal semantic family + Pareto dominance.
- Preserve raw composed solutions even after each frontier extraction step so later stages can compose `A frontier + B raw` and final `A raw + B leaf_unbuffered`.
- Expand maintained segment entry sets to support:
  - `all_frontier_entries`
  - `branch_buffered_entries`
  - `leaf_unbuffered_entries`
- Document the intended semantics directly near the maintained data structures and align variable naming with those semantics.
- Make H-tree composition able to choose from the correct maintained set, especially `leaf_unbuffered_entries` when leaf-stage branch buffering is not required.
- Add regression coverage for the pruning/semantic issue and add an extra real-tech test dedicated to `leaf_unbuffered`.

## Acceptance Criteria
- [ ] Segment/H-tree frontier pruning no longer drops entries that expose distinct later exact-join boundaries.
- [ ] `branch_buffered` and `leaf_unbuffered` terminal semantics remain isolated and are not merged during pruning or composition.
- [ ] H-tree build can select leaf-stage segment patterns from `leaf_unbuffered_entries`.
- [ ] New or updated tests cover the semantic regression and the real-tech `leaf_unbuffered` path.
- [ ] `./bin/icts_test_module_characterization` passes.
- [ ] `./bin/icts_test_flow_htree` passes.
- [ ] `./bin/icts_test_flow_htree_realtech` passes.
- [ ] `python3 ./.trellis/ecc_dev_tools/check.py check --path <touched-path>` passes for touched iCTS paths.
- [ ] Before/after test comparison is recorded in the handoff summary.
- [ ] Real-tech experiments isolate the effect of widening `max_slew` / `max_cap` versus increasing `slew_steps` / `cap_steps`.
- [ ] Real-tech experiments report overflow metrics, bottleneck join-pair intersections, and exact/relaxed join counts for `leaf_unbuffered` and `leaf all`.
- [ ] A conclusion is recorded on whether the dominant cause is bounds, resolution, or a combination of both.

## Technical Notes
- Keep the implementation within iCTS source/test modules; do not introduce external-module cleanup.
- Preserve existing no-exception/logging conventions.
- Favor local helper reuse inside `HTreeBuilder.cc` before introducing new shared modules.
- For the current experiment pass, `ecc_dev_tools` is not a gating criterion; prioritize reproducible real-tech diagnostics.
