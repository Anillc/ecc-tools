# Debug iCTS CTS Bench Failures Design

## Scope

This task debugs the 9 failed rows from the local ics55 CTS benchmark workspace.
The work may change:

- generated case SDCs or clock-selection scripts under
  `scripts/design/ics55_cts_bench`,
- iCTS source code when a real source defect is identified,
- focused tests for clock tracing, topology/H-tree behavior, or benchmark
  collection.

It must not reintroduce manual `use_netlist` mapping or classify behavior from
object-name substrings.

## Failure Classes

### SDC / Clock Selection Failures

Cases: `ad_top`, `mpw_asic_top`, `retrosoc_asic`, `top`.

Observed behavior:

- `ReadData` fails with `clock_trace_no_targets`.
- The selected SDC clock reaches no accepted CTS target nets.
- Logs list clock-like nets with sequential sinks as unowned by the SDC clock.

Resolution path:

1. Inspect DEF, Verilog, generated SDC, and trace logs.
2. Determine whether the selected top port is a pad/input that does not own the
   real internal clock net, or whether the intended internal net needs a
   generated clock/clock target in SDC.
3. Fix benchmark SDC generation or per-case SDC only when the change describes
   the real design clock relationship.
4. Rerun the case and verify `CTS Key Results.status = finished`.

### Single-Sink H-Tree Degeneration

Cases: `ascon`, `s1488`, `serdes_top`.

Observed behavior:

- SDC trace succeeds and finds a reachable clock sink net.
- Topology reduces the downstream domain to one H-tree sink / cluster buffer.
- H-tree build returns `no_h_tree_levels`; outer synthesis records
  `unknown_h_tree_failure`.

Resolution path:

1. Confirm whether a one-anchor domain is semantically complete without adding
   a downstream H-tree level.
2. If valid, implement a source fix that treats the single-anchor domain as a
   legal trivial topology and still commits the required source-to-anchor
   routing/buffer connection.
3. Add focused topology or flow tests so this does not become a silent success
   path for genuinely empty clocks.

### Strict Boundary Infeasible H-Tree

Cases: `ip2_TJUT_TOP`, `XSTop`.

Observed behavior:

- SDC trace succeeds.
- H-tree search has valid sink domains but strict root/slew boundary closure
  rejects every candidate for one or more domains.

Resolution path:

1. Read detail logs around frontier construction, compensation, and boundary
   rejection.
2. Decide whether infeasibility is caused by an overly strict algorithmic
   constraint, missing legal fallback, bad sink geometry, or an input setup
   issue.
3. Implement a real source fix only if it preserves timing/electrical legality.
   Do not weaken failure status or skip the failed domains.
4. If a robust fix requires a larger H-tree algorithm change, document the
   required整改方案 and evidence; do not mark those cases passed.

## Data and Reporting

Create a local per-case debug record under the task directory, for example
`research/failure_debug.md`, containing:

- before/after status,
- selected clock,
- relevant log excerpts by path and line,
- diagnosis,
- fix or整改方案,
- rerun command and result.

The final CSV sources remain under:

- `scripts/design/ics55_cts_bench/reports/cts_bench_summary.csv`
- `scripts/design/ics55_cts_bench/reports/cts_bench_failures.csv`
- `scripts/design/ics55_cts_bench/reports/run_status.csv`

## Rollback

- Benchmark SDC/script fixes can be rolled back by regenerating cases from the
  source DEF/Verilog and reverting `scripts/design/ics55_cts_bench/tools`.
- Source fixes are scoped to iCTS clock tracing/topology/H-tree code and must be
  isolated in git diff for review.
- Do not modify source data under `/nfs/share/home/liweiguo/ecc_cts_test`.
