# CTS optimization fast STA search quality

## Goal

Improve CTS fixed-topology buffer sizing quality using CTS fast STA as the only optimization timing source, and validate the behavior on `ics55_dev` with target skew values 80ps, 40ps, and 0ps.

Before changing the search algorithm, establish a clear baseline for the current fast STA CTS binary flow and compare it with the previous iSTA/char-backed behavior where data is available.

## Requirements

- Build on the committed fast STA CTS baseline, not the removed char-backed `module/buffer_sizing` implementation.
- Keep optimization as a peer flow stage after synthesis and before instantiation.
- Do not restore `module/buffer_sizing`, `CharTimingLookup`, or char-segment stitching for optimization.
- Do not introduce full iSTA inside the optimization loop.
- Do not introduce hand-written delay, slew, wire-delay, or cell-delay formulas in optimization.
- Optimization candidate evaluation must use CTS fast STA APIs for timing, slew, cap, skew, area, and power-related summaries.
- Preserve fixed topology: this task may resize existing CTS buffers but must not add or delete buffers or nets.
- Preserve no-new-violation legality for both cap and slew:
  - reject candidates that introduce a new cap or slew violation;
  - if the baseline already has a cap or slew violation, reject candidates that worsen the corresponding violated object.
- Allow bidirectional fixed-topology sizing for skew balancing:
  - late branches may be upsized to reduce late arrival;
  - early branches may be downsized or intentionally delayed to reduce early/late spread;
  - all accepted states must remain fast-STA-validated and cap/slew legal under the no-new-violation rule.
- Prefer batch-first search over exhaustive single-move greedy search; the candidate space should be expanded through bounded critical-frontier batches instead of a full global single-buffer scan.
- Use area as the cost metric when comparing legal states that both satisfy the target skew.
- If the target skew is unreachable under legal fixed-topology sizing, report the best reachable spread found by the implemented search.
- Default logs must remain concise:
  - runtime,
  - target skew,
  - before/optimized skew,
  - improvement,
  - iteration count,
  - accepted mutation count,
  - rejected candidate count,
  - cap/slew-rejected count,
  - area delta,
  - master transition distribution,
  - compact search counters when useful.
- Do not emit path dumps or large candidate tables by default.
- Development loop must not run `ecc_dev_tools`; run one final full `src/operation/iCTS` check only after implementation and binary pressure tests converge.
- Validate using the `scripts/design/ics55_dev/iEDA` binary flow, not gcd.
- Run pressure tests with target skew values 80ps, 40ps, and 0ps.

## Baseline Analysis Requirements

Before algorithm changes, collect and summarize the current fast STA binary baseline:

- `ics55_dev` total flow runtime and optimization runtime.
- CTS fast STA optimization setup and clock summary fields.
- Initial and optimized fast STA skew for 80ps, 40ps, and 0ps.
- Final reported iSTA setup/hold clock skew after CTS.
- Area delta and buffer master transition distribution.
- Cap and slew legality plus cap/slew-rejection counts.
- Current characterization/runtime changes visible in the binary logs.

Compare against previous iSTA/char-backed behavior where reproducible data exists:

- previous char characterization/runtime summaries from saved logs or prior committed task artifacts;
- previous optimization skew/runtime/area summaries from saved logs or prior committed task artifacts;
- if saved data is insufficient, document the missing data and define the exact reproducible comparison command or worktree setup needed.

## Acceptance Criteria

- [ ] Task PRD, design, and implementation plan are rewritten for fast STA-backed optimization.
- [ ] Current fast STA `ics55_dev` baseline is summarized before algorithm changes.
- [ ] Available previous iSTA/char-backed comparison data is summarized, with gaps called out explicitly.
- [ ] Search-quality improvement is implemented on top of `flow/optimization` and CTS fast STA APIs.
- [ ] The implementation does not restore removed char-backed optimization modules.
- [ ] Candidate acceptance remains fast-STA-validated and cap/slew-legal.
- [ ] The search supports both late-branch acceleration and early-branch delay/downsize balancing without introducing or worsening cap/slew violations.
- [ ] Default optimization logs remain concise.
- [ ] `scripts/design/ics55_dev/iEDA` builds or the existing binary provenance is stated.
- [ ] Binary pressure tests complete for 80ps, 40ps, and 0ps and results are summarized.
- [ ] Final full `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` is run after convergence.
- [ ] No `.trellis/spec` update is made unless implementation uncovers a reusable convention that should outlive this task.

## Notes

- The old char-backed task content was superseded by commit `2bc63329d feat(icts): add CTS fast STA timing and optimization`.
- This task now targets the current fast STA optimization architecture in `src/operation/iCTS/source/flow/optimization/`.
