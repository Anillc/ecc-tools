# Post-Change Runtime Results

## Scope

This report compares the saved baseline with two iSTA Liberty parser/linker
optimization slices:

- Slice A: replace per-call dynamic visitor dispatch tables in
  `visitGroup()`, `visitSimpleAttri()`, and `visitComplexAttri()`.
- Slice B: add scalar-pin fast path in `visitPin()` so ordinary pin names avoid
  regex matching.

No CTS source code was changed.

## Artifacts

- Baseline: `artifacts/baseline_perf/`
- Slice A: `artifacts/slice_a_dispatch/`
- Slice B: `artifacts/slice_b_pin_fast_path/`
- Final validation: `artifacts/final_dispatch_pin_fast_path/`

## CTS Runtime Overview

| Run | read_data | synthesis | optimization | instantiation | evaluation | total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline | 32.656 s | 10.470 s | 0.592 s | 0.097 s | 0.054 s | 43.883 s |
| Slice A | 10.877 s | 10.432 s | 0.578 s | 0.095 s | 0.052 s | 22.049 s |
| Slice B | 10.471 s | 10.485 s | 0.590 s | 0.100 s | 0.056 s | 21.718 s |
| Final | 10.469 s | 10.441 s | 0.589 s | 0.105 s | 0.056 s | 21.674 s |

## Liberty Load/Link Timestamp Split

| Run | Liberty load total | Liberty link total | Notes |
| --- | ---: | ---: | --- |
| Baseline | 5.524 s | 26.992 s | Original parser/linker path |
| Slice A | 5.612 s | 5.126 s | Dispatch fast path only |
| Slice B | 5.646 s | 4.682 s | Dispatch fast path plus scalar-pin fast path |
| Final | 5.644 s | 4.682 s | Latest binary after review-only line wrapping |

`read_data` improvement from baseline to final:

- Absolute reduction: `22.187 s`.
- Relative reduction: about `67.94%`.

CTS total improvement from baseline to final:

- Absolute reduction: `22.209 s`.
- Relative reduction: about `50.61%`.

Liberty link improvement from baseline to final:

- Absolute reduction: `22.310 s`.
- Relative reduction: about `82.66%`.

## QoR Metric Comparison

All tracked iCTS metrics match the baseline.

| Metric | Baseline | Final | Match |
| --- | ---: | ---: | --- |
| buffer_num | 2996 | 2996 | yes |
| clock_path_max_buffer | 8 | 8 | yes |
| clock_path_min_buffer | 7 | 7 | yes |
| max_clock_wirelength | 359608 | 359608 | yes |
| max_level_of_clock_tree | 8 | 8 | yes |
| total_clock_wirelength | 61790733.0 | 61790733.0 | yes |

## Validation

Completed:

```bash
ninja -C build liberty
ninja -C build iEDA
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The workload completed successfully for Slice A, Slice B, and the final
validation run.

Not run by request:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iSTA
```

An already-started iSTA `ecc_dev_tools` check was terminated after the user
clarified that iSTA should not receive ecc dev checks in this task.

## Conclusion

The confirmed bottleneck was iSTA Liberty link-time visitor overhead, not CTS
synthesis. Replacing per-node dynamic dispatch and avoiding regex on scalar pins
reduced Liberty link time from about `26.992 s` to about `4.682 s`, while
preserving the tracked iCTS QoR metrics.
