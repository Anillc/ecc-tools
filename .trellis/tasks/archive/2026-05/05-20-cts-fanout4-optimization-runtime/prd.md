# CTS fanout-4 optimization runtime convergence

## Goal

Fix the ics55_dev runtime blow-up when CTS max_fanout is 4 while preserving the fast fanout=32 behavior and optimization QoR constraints.

## Confirmed Facts

- The current `scripts/design/ics55_dev/iEDA_config/cts_default_config.json` sets `max_fanout` to `4`.
- A current Release build was used for the long run: `build/CMakeCache.txt` has `CMAKE_BUILD_TYPE=Release` and `CMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG`.
- The current `ics55_dev` run reached `Optimization: clock "core_clock" uses exact full-power batch solver`.
- With fanout 4, the current run met the 80ps target after the first accepted clock-sizing batch but kept searching area-reduction batches:
  - batch 1: skew `0.0792158 ns`, total trials `423`;
  - batch 11: skew `0.079665 ns`, total trials `5529`;
  - it was still running at iteration 12 when killed.
- The previously archived optimization runtime task recorded a fast 80ps baseline on `ics55_dev`: exact solver, initial skew `0.0883 ns`, optimized skew `0.0763 ns`, `7` mutations, `328` trials, optimization runtime about `4.2 s`, full run about `33 s`.
- Earlier fanout comparison evidence recorded fanout 4 as naturally larger than fanout 32, but still finite:
  - fanout 4 `/usr/bin/time` real `95.29 s`, CTS API elapsed `72.513 s`;
  - fanout 32 `/usr/bin/time` real `50.80 s`, CTS API elapsed `28.926 s`.
- The problem to fix is not "fanout 4 is larger than fanout 32"; it is the current optimization loop becoming unbounded for the small case after the skew target is already satisfied.

## Requirements

- Keep `max_fanout = 4` as the default `ics55_dev` validation setting.
- Diagnose the fanout 4 clock-sizing search behavior using local logs and source, especially:
  - post-target area-reduction search;
  - exact full-power trial count;
  - per-trial timing/power update cost;
  - candidate count changes caused by the deeper fanout 4 tree.
- Implement the smallest source change that prevents `ics55_dev` fanout 4 from spending minutes in the clock-sizing loop after meeting target skew.
- Preserve CTS timing legality guarantees:
  - no cap/slew regression relative to the fast STA baseline checks;
  - no accepted clock-sizing edit that makes the final skew miss the configured target after it has been met.
- Preserve fanout 32 behavior and avoid changing the default config simply to hide the runtime problem.
- Keep new names CTS-specific; do not introduce generic source names such as `snapshot`, `rollback`, `fallback`, `Input`, `Session`, `Internal`, `Support`, `Request`, or `Response`.

## Acceptance Criteria

- [x] `ics55_dev` with `max_fanout = 4` finishes successfully with `iCTS run successfully`.
- [x] The fanout 4 optimization phase no longer keeps running for hundreds or thousands of exact trials after the configured skew target is met.
- [x] The final fanout 4 report shows `status=finished`, target skew satisfied, and cap/slew legality preserved.
- [x] A fanout 32 check still finishes successfully and remains in the expected fast range.
- [x] Focused build/test targets covering optimization and FastSTA pass.
- [x] Final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passes with zero in-scope findings.
- [x] The task records measured fanout 4 and fanout 32 runtime/QoR before completion.

## Notes

- This task intentionally interrupts the broader CTS code normalization review. After it converges, return to the previous task and continue the remaining semantic/source cleanup checks.
