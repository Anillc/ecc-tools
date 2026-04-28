# CTS Fix Evaluation And Report Reference

Reference branch: `origin/cts_fix` at `1fdaf5c3c3fb5189c2fea767447bb40344664b52`.

## Scope

This note captures the behavior that the current CTS refactor must preserve or
adapt while keeping the new structured `cts.log` policy. The goal is behavior
parity, not a literal copy of old free-form log prose.

## External Feature Boundary

The new implementation must not modify external feature modules:

- `src/feature/database/feature_icts.h`
- `src/feature/parser/feature_parser_tools.cpp`
- any other file under `src/feature/**`

Internal CTS summary data can keep richer fields, but the external
`ieda_feature::CTSSummary` mapping must use only the pre-existing feature
fields.

## STA Evaluation Reference

Relevant reference files:

- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/source/module/evaluator/service/TimingAnalysisService.cc`

The reference evaluation setup performs these conceptual steps:

1. Prepare full-design timing state from iDB and SDC.
2. Set every SDC clock as propagated through `StaClock::set_is_propagated()`.
3. Refresh/update STA timing.
4. Build CTS RC trees for evaluation nets.
5. Report timing, which itself calls `updateTiming()` before invoking iSTA's
   timing report.

The important bug fix is the propagated-clock step. CTS evaluation must not
query ideal-clock arrivals after clock-tree writeback.

## Arrival Query Policy

`origin/cts_fix` queries clock arrival through the timing engine:

- analysis mode: max
- transition: rise
- clock name: the pin's owning clock

If iSTA returns no arrival, the reference logs a warning and returns `0.0` from
the API boundary. The current refactor must not add a second synthetic fallback
inside evaluation metrics. Missing STA data should be explicit in the log or
statistics output instead of being treated as a meaningful arrival.

## Skew And Latency Policy

The reference `latencySkewLog()` does not derive skew from geometry. It walks
iSTA clock groups and path data:

- iterate setup and hold modes;
- collect path-end data from each clock group;
- pick the worst path by `StaPathData::getSkew()`;
- report launch/capture clock arrival from the worst path's clock data;
- report worst skew from `getSkew()`;
- average the first ten worst path skews for a bounded average skew.

The refactor should expose equivalent statistics in the cleaned table style.

## Statistics Reports

`origin/cts_fix` writes report files from `StatisticsWriter::writeStatistics()`:

- the historical underscored wirelength report filename
- `cell_stats.rpt`
- `lib_cell_dist.rpt`
- `net_level.rpt`

The current task intentionally narrows this to:

- `wirelength.rpt`
- `cell_stats.rpt`
- `lib_cell_dist.rpt`

Do not write `net_level.rpt` for this task. Mirror the three required report
contents into `cts.log` using structured sections/tables.

## CTSAPI Report Reference

`origin/cts_fix` implements `CTSAPI::report(save_dir)` by delegating to
`CTSFlowRunner::report`. Its behavior:

- require an initialized CTS session;
- use `save_dir` when non-empty, otherwise the configured CTS work directory;
- create the output directory when needed;
- log `Start CTS Report`;
- reuse existing evaluator timing state if the current session has already
  been evaluated;
- otherwise rebuild evaluator timing before writing reports;
- call the evaluator statistics writer;
- log report runtime and memory;
- destroy a temporary timing engine if report had to create one.

The refactored flow does not need to recreate old class names, but it must
provide the same user-visible capability through `CTSAPI::report`.
