# Fix ics55 ECC CTS reproduction failures

## Goal

Use the latest locally built iEDA binary to reproduce the six ics55 ECC post-CTS benchmark cases with the same source configs, identify which cases still have real CTS/runtime issues, and provide a concrete issue list plus fix recommendations.

## Requirements

- Keep the six source case configs equivalent to the huangzhipeng benchmark configs.
- Do not change CTS tuning parameters such as `max_fanout`, `max_cap`, `enable_sink_clustering`, buffer list, or SDC contents for the baseline rerun.
- Only relocate runtime/output paths to the local `scripts/design/ics55_ecc_dev` workspace or temporary debug directories.
- Use the latest locally built binary from current source, not the older `bin/iEDA`, for the final six-case rerun.
- Fix blockers that prevent the latest binary from running the baseline configs far enough to produce meaningful results.
- Rerun all six cases and record per-case pass/fail status, log paths, and concrete failure reasons.
- Distinguish baseline reproduction failures from temporary A/B debug experiments.
- Provide recommended fixes for every real issue found.

## Confirmed Facts

- The local reproduction workspace exists under `scripts/design/ics55_ecc_dev`.
- The six cases are `ibex`, `jpeg_encoder`, `openroad_flow_scripts__flow_designs_src_ibex_sv`, `openroad_flow_scripts__flow_designs_src_jpeg`, `opentitan_earl_grey`, and `cv32e40p`.
- The older `bin/iEDA` reproduced CTS failed status for `ibex` and `cv32e40p`.
- The older binary failed those two cases because the SDC-resolved `clk_i` domain had only 3 or 4 direct sinks and default sink clustering reduced the downstream H-tree to one load.
- Current source contains a `trivial_single_load` branch in `HTree::build`, but the latest locally compiled binary currently crashes earlier in SDC clock tracing.
- gdb located the latest-binary crash at `ClockTraceResolve.cc:118`, comparing a Liberty expression `port_name` pointer value `0x42` as a C string.

## Acceptance Criteria

- [x] A Trellis task exists with PRD, design, and implementation plan.
- [x] Latest current-source binary is built successfully.
- [x] The latest binary no longer crashes in SDC clock tracing on the six baseline configs.
- [x] All six cases are rerun with unchanged source CTS configs and local output paths only.
- [x] A results report lists each case, status, log/output path, and concrete failure cause if any.
- [x] Recommendations are provided for every remaining real failure class.
- [x] Code changes, if any, are scoped and validated with focused build/rerun checks.

## Notes

- The final baseline rerun must not use temporary changes like `enable_sink_clustering=false` or `max_fanout=1`.
