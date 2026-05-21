# Run iCTS CTS Bench Cases Implementation Plan

## Preconditions

- User approves the planning artifacts. The multi-clock policy is already set:
  generate one 100 MHz SDC clock per case, selecting the largest clock net for
  multi-clock cases.
- The selected iEDA binary remains executable:
  `/home/liweiguo/project/ecc-tools/scripts/design/ics55_dev/iEDA`.
- The NFS PDK paths in the copied ics55 huge-dev DB config remain readable.

## Checklist

- [x] Recheck `git status` and classify dirty files before commit/archive.
- [x] Create `scripts/design/ics55_cts_bench`.
- [x] Copy baseline `iEDA_config` and required `script` files from
      `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev`.
- [x] Copy or symlink the selected latest `iEDA` binary according to the final
      workspace decision.
- [x] Add `tools/prepare_cases.py` to:
      - validate DEF/Verilog pairing,
      - create `cases/<case_name>/`,
      - copy DEF to `place.def`,
      - copy Verilog to `place.v`,
      - infer clock candidates from DEF,
      - choose the clock candidate with the largest DEF net connectivity for
        multi-clock cases,
      - write one-clock `default.sdc`,
      - write a clock-selection manifest for audit/debug.
- [x] Add `tools/run_cts_bench.py` to:
      - iterate cases deterministically,
      - generate per-case DB config,
      - run iEDA CTS-only Tcl,
      - capture stdout/stderr/runtime,
      - continue after failures.
- [x] Add `tools/collect_cts_metrics.py` to parse existing reports and write
      `reports/cts_bench_summary.csv`.
- [x] Add temporary final-evaluation reporting in iCTS/iSTA/iPA as needed so the
      run emits:
      - final iPA design power,
      - selected clock-network pin capacitance,
      - selected clock-network wire capacitance,
      - selected clock-network total capacitance,
      - fanout/cap/slew violation counts from STA interfaces when available.
- [x] Decide and implement the `Clk-Cap (fF)` path:
      - compute or report final clock pin cap + wire cap from the selected clock
        network,
      - use report parsing only as an explicitly marked fallback.
- [x] Implement `Pow. (uW)` from final design power by invoking iPA during final
      STA/evaluation; do not use HTree candidate power as the CSV source.
- [x] Implement fanout/cap/slew violation counts from STA interfaces first; only
      parse reports if the direct interface path is unavailable.
- [x] Run a smoke test on a small case such as `gcd`.
- [x] If smoke passes, run all 93 cases sequentially.
- [x] Review the CSV for schema, units, parse failures, and failed-case rows.
- [x] Run a final lightweight validation:
      - `git status --short`
      - case count check
      - CSV header check
      - smoke-run log/report existence check

## Final Execution Notes

- The final rerun used `--skip-power` for the later cases because iPA was
  hanging in the power stage. Those rows intentionally keep `Pow. (uW)` empty
  and record `power_source=missing_iPA_json`.
- Final CSV status is 93 rows: 84 passed and 9 `cts_failed` rows whose iEDA
  process completed but whose CTS key result status was `failed`.

## Validation Commands

Expected commands after implementation:

```bash
python3 scripts/design/ics55_cts_bench/tools/prepare_cases.py
python3 scripts/design/ics55_cts_bench/tools/run_cts_bench.py --case gcd
python3 scripts/design/ics55_cts_bench/tools/collect_cts_metrics.py
python3 scripts/design/ics55_cts_bench/tools/run_cts_bench.py --all
python3 scripts/design/ics55_cts_bench/tools/collect_cts_metrics.py
```

Exact flags may change during implementation, but equivalent prepare, smoke,
full-run, and collect commands must exist.

## Risk Points

- `top` and `XSTop` are multi-clock or ambiguous-clock designs; the largest-net
  selection manifest must make the chosen CTS clock auditable.
- `Clk-Cap (fF)` is not directly available as final `pin cap + wire cap` in the
  inspected cts key reports.
- The STA violation reports may be top-N reports rather than full violation
  dumps; counts must not silently undercount, so direct STA APIs are preferred.
- Invoking iPA during final evaluation may add runtime and may require ensuring
  the power engine is initialized against the final STA graph/database state.
- Large cases such as `XSTop` and `retrosoc_asic` may have long runtime or high
  memory use. The runner must preserve partial results and continue after
  failures.
- The huge-dev config references absolute NFS PDK files. If these paths become
  unavailable, no local copied PDK fallback currently exists.

## Review Gate Before Start

Before `task.py start`, confirm the user accepts this updated plan:

- One CSV row per case.
- Multi-clock cases constrain and run only the largest clock net.
- `Pow. (uW)` comes from final iPA design power.
- `Clk-Cap (fF)` is clock pin cap plus wire cap.
- Violation counts use STA interfaces first, with report parsing only as a
  marked fallback.
