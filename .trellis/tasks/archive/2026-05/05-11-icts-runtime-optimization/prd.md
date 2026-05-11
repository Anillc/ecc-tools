# optimize iCTS runtime bottlenecks

## Goal

Analyze and optimize the runtime bottlenecks of the iCTS flow driven by `scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl`, with the primary benchmark command:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The task should produce a measured runtime distribution, identify the dominant algorithmic bottleneck in the current codebase, and implement or prepare a focused optimization path that materially improves runtime without changing CTS correctness or QoR beyond explicitly accepted tolerance.

## Background / Known Context

- The target script initializes flow/db/lib/sdc/lef/def, runs `run_cts -config ./iEDA_config/cts_default_config.json -work_dir ./result/cts`, then writes DEF, Verilog, DB summary, tool metrics, and CTS reports.
- Current config uses `max_buf_tran = 0.5`, `max_sink_tran = 0.5`, `max_cap = 0.15`, `max_fanout = 32`, routing layers `5, 6`, and four buffer masters: `BUFX8H7L`, `BUFX12H7L`, `BUFX16H7L`, `BUFX20H7L`.
- Fresh baseline on 2026-05-11 reports one clock (`core_clock`), one clock net (`clk_i`), and `8751` sinks.
- Fresh CTS runtime distribution is: `read_data = 9.426 s`, `synthesis = 31.167 s`, `instantiation = 0.011 s`, `evaluation = 7.480 s`, `total = 48.088 s`; `cts_report` adds `1.433 s`.
- Fresh process runtime is `69.99 s` wall, `92.85 s` user, `15.96 s` sys, `155%` CPU, and `6174168 KB` max RSS.
- Existing logging shows `HTree::build = 17.079 s` and `CharBuilder::build = 7.472 s`, but it does not split the remaining `~14.09 s` synthesis time outside HTree or the `~9.61 s` HTree non-characterization residual.
- Current selected H-tree result in the fresh log: selected depth `5`, selected topology pattern id `10297477`, level segment pattern ids `522717,468375,28,24,6`, final frontier count `243981`, feasible solutions `130294`, inserted H-tree buffers `40`, final clock buffers `360`, total clock network wirelength `43151.203 um`, setup WNS `7.302292 ns`, hold WNS `0.008315 ns`.
- Prior archived task `.trellis/tasks/archive/2026-05/05-09-analyze-icts-htree-runtime-bottlenecks/` found the historical `0.5/0.5` bottleneck in H-tree synthesis and implemented opt1/opt2/opt3, reducing CTS synthesis from about `91.5 s` to about `31.8 s`.
- Prior rollback report identifies current production source as the opt3 baseline: exact Pareto scan, per-depth Pareto compression, and root-load signature cache are retained; later P1/P2/P3/P4/P6 experiments were rolled back or left investigation-only.
- Prior compact decision table says future runtime work must avoid late-only frontier cleanup unless it also avoids expensive upstream materialization; promising directions are characterization reachability / lazy characterization and proof-backed compose-time pruning.

## Assumptions

- Fresh measurement has been taken against the current `scripts/design/ics55_dev/iEDA` binary and stored under `artifacts/fresh_baseline/`.
- The MVP should focus on the `ics55_dev` relaxed-slew `0.5/0.5` benchmark unless the user asks for a broader design/config matrix.
- Runtime optimization must preserve selected topology/QoR for the MVP unless the user explicitly accepts approximate search.
- The first implementation slice should improve attribution before changing default pruning behavior, because the current fresh logs still leave large synthesis regions unmeasured.
- Temporary instrumentation or analyzers are allowed during the task, but production code should not keep noisy debug paths unless gated and justified.

## Requirements

- Capture a fresh baseline for the exact target command, including process wall/user/sys time and task-local copies of `cts.log`, `iCTS_metrics.json`, and relevant reports. (Done)
- Parse and report CTS stage-level runtime distribution from the fresh baseline. (Done)
- Attribute the remaining synthesis runtime to code paths using log markers, source inspection, and, where useful, focused instrumentation or external profiling.
- Review prior H-tree runtime optimization and dominance-pruning attempts before designing new pruning work.
- Pick an optimization direction that is materially different from failed or low-value attempts already recorded in archived tasks.
- Preserve CTS correctness and QoR: selected H-tree structure, final clock buffer count, wirelength, setup/hold WNS, and key timing/power metrics must be compared before and after any implementation.
- Keep iCTS changes within the established `setup -> synthesis -> instantiation -> evaluation -> report` architecture and follow backend specs for ownership, logging, error handling, and CMake.

## Acceptance Criteria

- [x] Task artifacts contain a fresh baseline run for the target command with `run.log`, `time.txt`, `stage_markers.log`, `cts.log`, and metrics/report copies.
- [x] A task-local research note summarizes fresh runtime distribution and identifies the dominant stage and likely substage bottleneck.
- [x] `design.md` defines a focused optimization hypothesis with source boundaries, correctness contract, validation matrix, and rollback plan.
- [x] `implement.md` lists ordered implementation and validation steps before `task.py start`.
- [x] If code is changed, benchmark after the change shows a material runtime improvement or the task records why the hypothesis was falsified and what stopped the loop.
- [x] QoR comparison covers selected H-tree ids, selected delay/power, final buffer count, total clock wirelength, setup WNS, and hold WNS.
- [x] Final validation includes focused iCTS tests/builds and the required final `src/operation/iCTS` checker pass before handoff.

## Out of Scope

- Broad refactors outside iCTS H-tree / characterization / synthesis flow unless measurements prove they dominate runtime.
- External iSTA/iDB changes except for narrowly scoped instrumentation required to attribute runtime.
- Approximate pruning, sparse sampling, or depth caps as default production behavior without explicit user approval.
- Reintroducing post-opt3 experimental code from archived tasks without a new proof or evidence plan.

## Research References

- `research/runtime-baseline.md` - current task runtime attribution note; to be updated after the fresh baseline run.
- `.trellis/tasks/archive/2026-05/05-09-analyze-icts-htree-runtime-bottlenecks/reports/rollback_to_opt3_report.md` - source of truth for current opt3 baseline and rolled-back experiments.
- `.trellis/tasks/archive/2026-05/05-09-icts-htree-dominance-pruning-research/research/experiment_decision_table.md` - compact table of failed/stopped runtime-pruning attempts and future-loop preface requirements.
