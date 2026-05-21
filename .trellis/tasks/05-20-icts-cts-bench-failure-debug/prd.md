# Debug iCTS CTS Bench Failures

## Goal

Analyze and resolve the remaining failed iCTS CTS benchmark cases from
`scripts/design/ics55_cts_bench` so the benchmark can run all cases through CTS
without masking real failures.

## Requirements

- Investigate each failed case from the latest benchmark CSV:
  - `XSTop`
  - `ad_top`
  - `ascon`
  - `ip2_TJUT_TOP`
  - `mpw_asic_top`
  - `retrosoc_asic`
  - `s1488`
  - `serdes_top`
  - `top`
- For every failed case, identify the first failing stage and root cause:
  SDC/bench input, clock selection, iCTS clock tracing, topology/H-tree
  synthesis, evaluation/reporting, or another source-code defect.
- If a case fails because the generated SDC or benchmark case setup selected
  the wrong clock or failed to describe an existing clock domain, fix the
  generated per-case SDC or clock-selection logic and rerun that case.
- If a case fails because iCTS source behavior is wrong, do not bypass the
  failure by weakening status checks, skipping synthesis, or marking failures
  as success. Implement a real fix when the scope is clear and low risk; if the
  fix requires a larger algorithmic change, produce a concrete整改方案 with
  evidence, affected source areas, expected behavior, and validation plan.
- Preserve the user requirement that `use_netlist` is not reintroduced.
- Preserve the structural preclustered-sink reuse policy: no object-name or
  net-name substring hard-coding for CTS behavior.
- Keep benchmark reporting honest: a case only counts as passed when
  `CTS Key Results.status` is `finished`.
- Rerun fixed cases individually first, then rerun the remaining failure set or
  full benchmark as needed to prove the result.
- Maintain a per-case debug record with:
  - selected clock and SDC target,
  - failing stage before fix,
  - root cause,
  - change made or整改方案,
  - final rerun status,
  - relevant log/report paths.

## Acceptance Criteria

- [ ] Each of the 9 failed cases has a documented diagnosis and outcome.
- [ ] All cases whose issue is SDC or benchmark setup are fixed in the
      benchmark workspace/scripts and rerun successfully.
- [ ] All source-code fixes are covered by focused tests or a focused
      benchmark rerun proving the fixed behavior.
- [ ] No code or config change reintroduces `use_netlist`, `net_list`, or
      name-pattern based CTS behavior.
- [ ] The final benchmark CSV records no remaining `cts_failed` rows for issues
      that can be fixed within this task.
- [ ] Any remaining source-level algorithm issue that cannot be safely fixed in
      this task has a concrete整改方案 rather than a bypass.
- [ ] The final user report summarizes problem category, solution, and effect
      for every failed case.

## Notes

- Latest parent benchmark status before this task: 93 total rows, 84 passed,
  9 `cts_failed`.
- Initial failure categories observed from `cts.log`:
  - `clock_trace_no_targets`: `ad_top`, `mpw_asic_top`, `retrosoc_asic`,
    `top`
  - single-sink-domain `no_h_tree_levels` surfaced as
    `unknown_h_tree_failure`: `ascon`, `s1488`, `serdes_top`
  - strict H-tree boundary infeasible:
    `ip2_TJUT_TOP`, `XSTop`
- Power collection is not part of this task unless a fix requires rerunning
  power. Continue using `--skip-power` for debug reruns to avoid the known iPA
  hang while focusing on CTS pass/fail.
