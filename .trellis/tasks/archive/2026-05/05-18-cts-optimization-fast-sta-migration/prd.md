# CTS optimization fast STA migration

## Goal

Migrate CTS buffer sizing optimization from characterization lookup timing to the validated CTS fast STA incremental timing/power口径, then evaluate skew pressure behavior on the ics55 dev binary at 80ps, 40ps, and 0ps target skew settings.

## Requirements

- Depend on parent task `05-18-cts-fast-sta-timing-power` for validated fast STA build/query/update APIs.
- Replace optimization timing evaluation with fast STA incremental update queries. The optimization algorithm should not compute delay, slew, or skew using its own formulas.
- Keep topology fixed. Do not add or delete buffers in this task.
- Use fast STA cap legality and timing/power data for accept/reject decisions after buffer master changes.
- Preserve area as the primary cost metric for optimization decisions.
- Support target-skew optimization semantics:
  - if the target skew is reachable, minimize area cost while meeting target skew,
  - if the target skew is unreachable, minimize final skew spread under legal sizing changes.
- Validate with the ics55 dev binary command:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- Run the binary at target skew settings of 80ps, 40ps, and 0ps.
- Report initial skew, final skew, target skew, area/cost delta, cap legality status, changed buffer master distribution, and optimization runtime for each run.
- Do not run `ecc_dev_tools` during development. The final full check is owned by the parent completion gate after both tasks converge.

## Acceptance Criteria

- [ ] Optimization no longer depends on `CharTimingLookup` for move evaluation after fast STA migration.
- [ ] Optimization applies candidate buffer master changes through fast STA incremental update APIs and uses fast STA results for accept/reject.
- [ ] No optimization code introduces independent delay/slew formulas.
- [ ] Cap legality is checked through fast STA state and no new cap violation is accepted.
- [ ] Area cost is reported and used as the primary cost term.
- [ ] The fixed-topology constraint is preserved.
- [ ] The ics55 dev binary is run at 80ps, 40ps, and 0ps target skew settings.
- [ ] A result report summarizes skew/cost/runtime/change distribution for all three target settings.
- [ ] Final completion is blocked until the parent final `src/operation/iCTS` ecc dev check passes.

## Out of Scope

- Fast STA timing/power implementation itself.
- OpenSTA alignment cases owned by the parent task.
- Buffer insertion, buffer deletion, routing topology changes, or full STA optimization.
- Current-source timing, SI, multi-corner optimization, or power-driven optimization beyond area reporting/cost.

## Dependency

This child task starts after parent fast STA exposes validated APIs for clock context build, skew query, cap status query, buffer master changes, area/power query, and incremental recomputation.
