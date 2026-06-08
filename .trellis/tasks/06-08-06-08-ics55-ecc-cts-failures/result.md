# Result Report

## Final Baseline Rerun

Command:

```bash
python3 scripts/design/ics55_ecc_dev/tools/run_ecc_cts.py --all --force --ieda-binary scripts/design/ics55_dev/iEDA --timeout 300
```

The final run used the rebuilt current-source binary at `scripts/design/ics55_dev/iEDA`. The local run configs under
`scripts/design/ics55_ecc_dev/cases/<case>/run_config` keep the source CTS configs unchanged; all six local
`cts_default_config.json` files are byte-identical to the corresponding huangzhipeng source configs. Only local runtime/output paths
point into `scripts/design/ics55_ecc_dev`.

| Case | Status | Exit | Runtime | Root Driver Period | Final Buffers | Clock Wirelength |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| cv32e40p | passed | 0 | 56.43s | 9.5238ns | 90 | 19430.548um |
| ibex | passed | 0 | 27.61s | 8.3333ns | 77 | 15017.428um |
| jpeg_encoder | passed | 0 | 140.56s | 5.5556ns | 180 | 39169.703um |
| openroad_flow_scripts__flow_designs_src_ibex_sv | passed | 0 | 27.62s | 10.0000ns | 75 | 15627.774um |
| openroad_flow_scripts__flow_designs_src_jpeg | passed | 0 | 177.48s | 5.5556ns | 180 | 39816.301um |
| opentitan_earl_grey | passed | 0 | 33.93s | 4.7619ns | 71 | 13388.407um |

Fresh status CSV:

- `scripts/design/ics55_ecc_dev/reports/run_status.csv`

Fresh CTS logs:

- `scripts/design/ics55_ecc_dev/cases/<case>/result/data/cts/cts.log`
- `scripts/design/ics55_ecc_dev/cases/<case>/run_iCTS_ecc.stdout.log`

All six cases produced `_CTS.def.gz` and `_CTS.v` outputs under:

- `scripts/design/ics55_ecc_dev/cases/<case>/result/output/`

## Actual Issues Found

### 1. Latest binary crashed during SDC clock trace before CTS

Scope:

- Affected the latest locally rebuilt binary before this task's code fix.
- Blocked meaningful latest-binary rerun of all six configs.

Root cause:

- iCTS clock tracing walked Liberty boolean expressions by treating `LibertyExpr::left` and `LibertyExpr::right` as stable
  `LibertyExpr*` nodes.
- The current C++ Liberty parser stores raw-expression handles in those fields. Direct traversal can expose invalid data such as a
  bogus `port_name` pointer and crash during `port_name == current->port_name`.

Fix applied:

- `ClockTraceResolve.cc` and `WrapperClockReader.cc` now traverse child expressions through `liberty_get_expr_left()` /
  `liberty_get_expr_right()` and release returned child views with `liberty_free_expr()`.
- The traversal is iterative to satisfy the iCTS no-recursion checker.

Recommendation:

- Keep all future iCTS Liberty expression traversal inside adapter code and use the accessor/free contract.

### 2. SDC numeric expression parsing misclassified successful numbers as failures

Scope:

- Affected every one of the six benchmark SDCs because they use:

```tcl
set clk_period [expr 1000.0 / $clk_freq_mhz]
```

Root cause:

- `ParseDoubleValue()` and `ParseIntValue()` parsed the numeric token, then always ran `stream >> std::ws`.
- When the stream was already at EOF after successful numeric extraction, the extra whitespace extraction caused the code to treat
  the parse as failed.
- `[expr ...]` then emitted `unresolved_expr:<value>` and returned `"0"`.
- The downstream clock period was not resolved from SDC and CTS fell back to 10ns. For `openroad_flow_scripts__flow_designs_src_ibex_sv`
  this matched the expected 10ns by coincidence; for the other five cases it was wrong.

Fix applied:

- Numeric parsing now accepts EOF immediately after successful numeric extraction and only consumes trailing whitespace when needed.
- `[expr]` output now uses `std::numeric_limits<double>::max_digits10` precision so periods round-trip without truncation.
- Existing flow SDC trace regression now uses the benchmark-style expression and validates `1000.0 / 120.0`.

Recommendation:

- Keep this regression test in place. Add direct parser tests if the SDC subset parser gets a dedicated test target later.

### 3. The two historical CTS failures were old-binary failures, not remaining baseline failures

Scope:

- Historical old `bin/iEDA` failed `ibex` and `cv32e40p`.

Root cause from old-run debugging:

- The SDC-resolved source side of `clk_i` had only 2 or 3 direct sequential sinks, then clock tracing correctly crossed a comb output to
  the real downstream clock net.
- With old binary behavior, sink clustering could reduce the downstream H-tree to one load and the old HTree path returned
  `no_h_tree_levels` / `unknown_h_tree_failure`.

Current result:

- The rebuilt latest binary contains `trivial_single_load`.
- `ibex` and `cv32e40p` both pass with unchanged CTS configs:
  - `ibex`: `clk_i` traces through `_13407_/Y` to `clk`, accepted 1985 sequential sinks.
  - `cv32e40p`: `clk_i` traces through `_58756_/Z` to `core_i.clk`, accepted 2304 sequential sinks.

Recommendation:

- Do not change fanout, sink clustering, or CTS tuning for these two cases. Use the latest binary with the crash and SDC parser fixes.

## Validation

- Built current-source binary: `ninja -C build iEDA icts_test_flow`
- Regression test passed:
  `./bin/icts_test_flow --gtest_filter=FlowTest.SdcClockTraceResolvesVariableGetPortsToDownstreamClockTarget --gtest_color=no`
- Latest binary contains HTree single-load support:
  `strings scripts/design/ics55_dev/iEDA | rg 'trivial_single_load'`
- Six-case final rerun passed with unchanged CTS configs.
- `rg unresolved_expr scripts/design/ics55_ecc_dev/cases/*/result/data/cts/cts.log` has no matches after the final rerun.
- Full iCTS checker passed:
  `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
