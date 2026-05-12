# small-fanout H-tree legality root-cause debugging

## Goal

Build a focused debugging task for the CTS H-tree failure where small `max_fanout` values, currently reproduced with `max_fanout = 4`, prevent CTS from selecting any legal H-tree topology and end with `no_legal_depth_candidates`.

The task should establish the true root cause, not only the surface symptom, and should produce enough evidence to decide whether the fix belongs in configuration guidance, sink clustering, H-tree depth/topology generation, sink-load-region legality filtering, fallback behavior, or diagnostics.

## Background / Known Context

- The flow under investigation is:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- `run_iCTS_dev.tcl` hard-codes `WORKSPACE` to `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev` and reads `iEDA_config/cts_default_config.json`.
- The current dev config has `"max_fanout": "4"`.
- Latest dev CTS log confirms runtime config `max_fanout = 4`.
- CTS input has `8751` regular sinks; sink clustering reduces this to `3010` H-tree loads.
- H-tree topology generation succeeds for `3010` loads with depth `11` and `2048` leaves.
- H-tree depth exploration evaluates depths `11`, `10`, `9`, and `8`.
- For every evaluated depth, `Filter sink-load region` produces `candidate_frontier_entries = 0` and `feasible_frontier_entries = 0`, despite large raw frontiers.
- CTS fails with `H-tree build failed: no_legal_depth_candidates`.
- The filtering path uses `CONFIG_INST.get_max_fanout()` in `SinkLoadRegion.cc` and marks a hard monotone failure when a boundary load group exceeds `max_fanout`.

## Problem Statement

With small fanout constraints, the current H-tree implementation can construct a geometric topology and characterize large candidate frontiers, but all candidates are removed by sink-load-region legality before global selection. The current logs show counts but do not expose enough detail to answer:

- Which boundary groups exceed fanout and by how much.
- Whether the violation is caused by topology leaf capacity, sink clustering behavior, bottom-most buffered level choice, pattern pruning, or an overly strict legality model.
- Whether a legal solution exists if the H-tree explores deeper, reclusters differently, changes local-buffer semantics, or applies a hierarchical load split.
- Whether the flow should fail earlier with a clearer infeasibility diagnostic.

## Requirements

- Reproduce the failure using the dev `ics55_dev` flow and record exact config, command, log path, binary path, and timestamp.
- Preserve a baseline successful comparison using a known legal fanout, such as `max_fanout = 32`, or explicitly reference an existing successful run if rerunning is not necessary.
- Instrument or otherwise collect enough detail to identify the first and dominant legality rejection causes for small fanout.
- Measure boundary group fanout distribution per evaluated depth and per relevant bottom-most buffered level.
- Analyze the H-tree leaf fanout-relative algorithm and determine whether the upstream sink clustering algorithm can make small fanout constraints over-tight or structurally infeasible.
- Analyze whether H-tree construction checks or optimizes fanout at intermediate levels, not only at leaf / sink-load-region boundaries.
- Determine whether the issue is mathematically infeasible under the current topology and clustering, or whether a legal topology is pruned/missed by the current algorithm.
- Evaluate at least these hypotheses:
  - `max_fanout=4` is below the minimum load grouping achievable by current H-tree depth and sink clustering.
  - Sink clustering generates local buffer loads that are legal individually but still too dense for downstream H-tree boundary groups.
  - The leaf fanout-relative algorithm relies on clustered local-buffer loads in a way that amplifies fanout pressure when `max_fanout` is small.
  - H-tree intermediate levels do not enforce fanout during topology/pattern construction, so illegality is discovered only after many candidates reach leaf-region filtering.
  - The monotone hard-fail pruning by bottom-most buffered level removes candidates that could become legal through a different segment pattern or depth.
  - The depth exploration window does not reach a depth that can satisfy small fanout.
  - The legality model applies the user `max_fanout` at a boundary where a different derived fanout limit should be used.
- Produce a root-cause report with evidence and recommended fix direction.
- If code changes are proposed, define focused tests that fail before the fix and pass after it.
- During debugging, do not use broad `ecc dev` checks as the task gate; use targeted evidence collection and the specified flow instead.
- Final acceptance must be based on this exact flow command:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

## Acceptance Criteria

- [ ] The task artifacts document the exact dev flow and config path used for reproduction.
- [ ] A failure reproduction record includes log excerpts for `max_fanout = 4`, `no_legal_depth_candidates`, evaluated depths, and zero post-filter candidates.
- [ ] A baseline record identifies at least one passing configuration and key differences from the failing case.
- [ ] The investigation can report the first rejection reason and aggregate rejection distribution for sink-load-region filtering.
- [ ] The investigation identifies the smallest observed or estimated boundary load group fanout that blocks legality under `max_fanout = 4`.
- [ ] The root-cause report explicitly explains whether sink clustering contributes to small-fanout failure in the H-tree leaf fanout-relative algorithm.
- [ ] The root-cause report explicitly explains whether intermediate H-tree levels consider fanout, and whether missing intermediate fanout handling contributes to late all-candidate rejection.
- [ ] Each listed hypothesis is confirmed, rejected, or marked unresolved with concrete evidence.
- [ ] The final report recommends one primary fix path and at least one fallback or mitigation.
- [ ] Any implementation plan avoids broad `ecc dev` checks during debugging and uses targeted investigation commands instead.
- [ ] Final validation uses exactly `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` and verifies CTS internal success.

## Out of Scope

- Broad H-tree performance optimization unrelated to small fanout legality.
- Changing default fanout policy without root-cause evidence.
- Replacing the H-tree algorithm wholesale.
- Treating the process exit code alone as success; CTS internal status must be checked.

## Research References

- `research/fanout4-initial-evidence.md`
- `research/root-cause-report.md`
- `design.md`
- `implement.md`
