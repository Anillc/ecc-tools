# CTS huge design optimization performance

## Goal

Research and plan a scalable CTS optimization timing-control strategy for huge designs with runtime advantage and non-regression guarantees.

The immediate user need is not to implement another heuristic immediately. The task should first identify why the current scalable optimization still underperforms on `ics55_huge_dev`, compare algorithmic options from CTS / useful-skew / buffer-sizing literature and tools, and recommend a concrete next implementation direction that can work for both huge and small designs.

## Confirmed Facts

- Parent task: `.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing`.
- Preceding runtime task was committed as `0a332101d perf(icts): improve CTS optimization huge runtime`.
- Current CTS optimization is under `src/operation/iCTS/source/flow/optimization/Optimization.cc`.
- The latest huge run uses `scalable_timing_only_batch`:
  - `ics55_huge_dev` fast STA graph has `211483` nodes, `44737` nets, `122010` sinks, and `44736` optimizable buffers.
  - 80ps target run improved fast STA skew from `0.1154 ns` to `0.1060 ns`, but did not meet the `0.0800 ns` target.
  - It ran `32` exact timing-only trials, with `456.9133 s` spent in batch trial evaluation and `502.000 s` optimization runtime.
  - Many top scored candidates were legal but worsened skew dramatically, often to `0.17-0.32 ns`.
- Exact trial runtime on huge is roughly `14 s` per candidate even after removing power from the trial loop.
- Full iSTA must not be introduced into the optimization loop.
- No-new-cap and no-new-slew legality constraints must be preserved.

## Requirements

- Analyze the current optimization weaknesses using local code and saved huge logs.
- Research timing-control and CTS optimization strategies relevant to:
  - clock skew scheduling / useful skew;
  - delay insertion and intentional latency balancing;
  - buffer sizing / buffer insertion dynamic programming;
  - practical open-source CTS behavior such as TritonCTS/OpenROAD characterization and repair flow.
- Recommend a strategy that:
  - can run on `ics55_huge_dev` without hundreds of exact trials;
  - has a strict non-regression acceptance gate;
  - generalizes to small and huge designs;
  - avoids full iSTA in the loop;
  - keeps cap/slew legality checks;
  - prefers no topology change for the first implementation stage, unless evidence shows topology change is necessary.
- Produce a report that distinguishes:
  - baseline issues;
  - candidate strategies considered;
  - selected strategy and rationale;
  - expected runtime/QoR behavior;
  - implementation phases and validation plan.

## Acceptance Criteria

- [x] A research report exists under `research/` with local evidence and external references.
- [x] `design.md` captures the recommended architecture and semantic boundaries.
- [x] `implement.md` captures implementation phases and validation commands.
- [x] The report explicitly explains how the strategy avoids skew/cap/slew regression.
- [x] The report includes a huge-case runtime/QoR expectation and an experimental matrix for 80ps/40ps/0ps.

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
