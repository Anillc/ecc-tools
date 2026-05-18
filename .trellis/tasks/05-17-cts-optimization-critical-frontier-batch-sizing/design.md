# Design: CTS optimization fast STA search quality

## Current Architecture

The current optimization flow is fast STA-backed fixed-topology buffer sizing:

```text
synthesis result -> flow/optimization -> FastStaAdapter clock context -> candidate sizing trials -> FastStaAdapter incremental update -> final mutations -> instantiation
```

`flow/optimization` owns flow integration, config access, design mutation application, and reporting. CTS fast STA owns timing, slew, cap, skew, area, and power summaries. Optimization must not duplicate timing formulas or call full iSTA in the candidate loop.

The removed char-backed optimization stack is no longer part of the design:

```text
source/module/buffer_sizing
CharTimingLookup
TreeBufferSizing
char segment additive stitching for optimization
```

## Baseline First

Before changing the search algorithm, collect a baseline from the current binary flow:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Run or reuse logs for skew targets 80ps, 40ps, and 0ps. Extract:

- total flow runtime;
- optimization runtime;
- fast STA timing source and candidate count;
- initial/optimized fast STA skew;
- accepted mutations, rejected candidates, cap rejections;
- area delta and transition distribution;
- final iSTA setup/hold skew;
- characterization/runtime summary fields visible in `cts.log`.

Then compare against previous behavior. The expected sources are, in order:

1. saved `ics55_dev` logs from prior tasks;
2. prior task result artifacts;
3. reproducible runs from old commits in a separate worktree if saved data is insufficient.

The comparison must explicitly distinguish three timing口径:

- current CTS fast STA optimization timing;
- previous char-backed optimization timing;
- final ordinary iSTA CTS reporting timing.

## Optimization Direction

The current solver tries every single buffer/master candidate and accepts the best single move that improves the target objective. This can stop at a local optimum when no single move improves global skew, even if a coordinated set of moves would. The improved solver should therefore be batch-first rather than single-move-first.

The improved solver should remain conservative but broaden the search:

1. Evaluate all sink arrivals from fast STA.
2. Build bounded critical frontiers on both sides of the spread:
   - late sinks near the max arrival;
   - early sinks near the min arrival.
3. Collect candidate buffers from shared-prefix/LCA-style paths where that topology information is available from the fast STA clock context or CTS DAG.
4. Generate structured batch candidates directly instead of doing a full global single-buffer scan:
   - upsize late-side path, prefix, level, or shared-branch buffers to reduce late arrivals;
   - downsize or intentionally delay early-side path, prefix, level, or shared-branch buffers to reduce early/late spread.
5. Evaluate each complete batch through fast STA incremental updates or a fast STA batch transaction.
6. Commit only states that improve the full fast STA objective and do not introduce or worsen cap/slew violations.
7. Prefer lower area among legal states that satisfy target skew.

The exact frontier construction can evolve during implementation, but all accepted states must be validated by fast STA after the complete batch is applied.

## Boundaries

`flow/optimization` may read `CONFIG_INST`, `DESIGN_INST`, and `STA_ADAPTER_INST` because it is a flow boundary and already applies final design mutations.

`database/adapter/fast_sta` remains the single owner of timing, slew, cap, skew, power, and incremental recomputation semantics.

No new module layer is required unless the fast STA search policy becomes large enough to justify extraction. If extraction is needed later, it must receive explicit problem/state APIs and must not read runtime singletons directly.

## Reporting

Default output should stay compact. Useful summaries are:

- target skew;
- initial and optimized fast STA skew;
- runtime;
- area delta;
- accepted mutation count;
- rejected candidate count;
- cap/slew-rejected count;
- batch/frontier trial counters if added;
- master transition distribution.

Detailed sink paths, path dumps, and candidate tables are temporary debug instrumentation only and should not remain in default logs.

## Validation

Development validation uses targeted builds and the `ics55_dev` binary pressure matrix. Do not run `ecc_dev_tools` until the final convergence check.

Pressure matrix:

```text
80ps -> default target reachability
40ps -> tight target pressure
0ps  -> best-reachable spread pressure
```

Each run should record both fast STA optimization metrics and final ordinary iSTA CTS reporting skew, because these are intentionally different timing口径.
