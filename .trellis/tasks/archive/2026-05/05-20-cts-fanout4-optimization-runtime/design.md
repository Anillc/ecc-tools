# Design: CTS fanout-4 optimization runtime convergence

## Boundary

The fix belongs in the CTS clock-sizing optimization path under `src/operation/iCTS/source/flow/optimization`. The fanout 4 topology creates a deeper and denser resizable-buffer search space, but the synthesis topology and config should remain valid. Do not change `max_fanout` to 32 as the solution.

FastSTA remains the clock-sizing truth source. Exact accepted edits must still be checked through the existing FastSTA timing/power state capture and cap/slew legality checks.

## Runtime Cause To Verify

The current exact solver has no immediate stop when target skew is met. After the first accepted batch meets 80ps, trial preference switches to area reduction while keeping target compliance. With fanout 4, the deeper tree provides many legal area-reduction edits, so the solver continues scanning hundreds of full-power candidates per iteration.

The likely fix is to make the post-target search bounded by CTS semantics:

- before target is met, optimize skew under cap/slew legality;
- after target is met, accept only a limited amount of area recovery, or stop immediately for small cases when skew target is satisfied;
- never let area recovery consume the full exact-trial budget on `ics55_dev`.

## Proposed Source Shape

Add a CTS-specific stop policy to `OptimizationOptions` and apply it in `SolveClock`:

- a boolean or small numeric option with CTS naming, e.g. `finish_when_target_skew_met` or `max_target_met_area_batches`;
- default should protect small fanout 4 without weakening huge-case scalable behavior;
- if a bounded area pass is kept, it must be low by default and measured.

Prefer the simplest behavior first: stop the exact full-power small-case solver once the target skew is met after an accepted batch. This matches the user-visible CTS objective and removes the open-ended area pass. If QoR evidence shows meaningful area recovery is required, use a very small CTS-specific post-target budget instead.

## Compatibility

- Fanout 4 remains legal and keeps the default config.
- Fanout 32 still runs through the same synthesis and optimization flow.
- Large-case scalable solver behavior should not be changed unless the shared stop policy is clearly needed there too.
- Report fields and existing task artifacts should be updated only where needed for measured evidence.

## Validation Data

Record for fanout 4 and fanout 32:

- full command and config value;
- solver mode;
- initial and final skew;
- accepted edit count and accepted batch count;
- exact trial count;
- optimization runtime and full CTS runtime;
- final `iCTS run successfully` status.
