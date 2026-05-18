# Results: huge design optimization performance

## 2026-05-18 Huge 80ps V1

Code version:

- Topology-aware target-window candidate scoring in `src/operation/iCTS/source/flow/optimization/Optimization.cc`.
- Exact acceptance remains timing-only fast STA with cap/slew non-regression checks.
- No ecc dev check was run in this loop per user instruction.

Run command:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev
CTS_CONFIG=/home/liweiguo/project/ecc-tools/.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/run_configs/cts_opt_huge_80ps.json \
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Artifacts:

- Log: `/home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev/.trellis_window_huge_80ps_v1.log`
- Result directory: `/home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev/result_trellis_window_huge_80ps_v1`

Comparison against previous huge v3 baseline:

| Metric | v3 baseline | window V1 |
|---|---:|---:|
| initial skew | 0.1154 ns | 0.1154 ns |
| optimized skew | 0.1060 ns | 0.1031 ns |
| improvement | 0.0094 ns | 0.0123 ns |
| exact timing-only trials | 32 | 24 |
| accepted batches | 4 | 2 |
| accepted mutations | 4 | 6 |
| cap rejected | 0 | 0 |
| slew rejected | 0 | 0 |
| batch trial eval runtime | 456.9133 s | 304.0171 s |
| optimization runtime | 502.000 s | 350.2467 s |
| full run runtime | 741.13 s | 590.44 s |

Observed solver behavior:

- First iteration target window: 0.603556 ns to 0.695915 ns, staged skew 0.0923587 ns.
- Branch classification found 223 late-pure buffers, 552 early-pure buffers, 4 mixed buffers, and 43957 neutral buffers.
- The exact gate rejected all cap/slew-risk cases cleanly; accepted transitions were only upsizes:
  - `BUFX12H7L -> BUFX20H7L`: 5
  - `BUFX8H7L -> BUFX20H7L`: 1
- Several high-score early/downsize candidates were legal but worsened skew, so the next tuning opportunity is to lower early/downsize priority before target is met or diversify exact verification toward more late/upsize candidates.
