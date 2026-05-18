# CTS optimization fast STA migration results

## Command

Each validation run used:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The target skew was changed through `scripts/design/ics55_dev/iEDA_config/cts_default_config.json` for each run. The local config has been restored to the default `0.08` after the matrix.

Full run logs are saved under the ignored design script directory:

- `scripts/design/ics55_dev/.trellis_fast_sta_opt_80ps.log`
- `scripts/design/ics55_dev/.trellis_fast_sta_opt_40ps.log`
- `scripts/design/ics55_dev/.trellis_fast_sta_opt_0ps.log`

## Matrix

| Target | Initial fast STA skew | Optimized fast STA skew | Improvement | Target met | Area delta | Cap legal | Accepted | Trials | Optimization runtime | Transition distribution | Final iSTA setup/hold skew |
|---:|---:|---:|---:|---|---:|---|---:|---:|---:|---|---|
| 80ps | 0.0883 ns | 0.0800 ns | 0.0084 ns | true | +4.4800 um^2 | true | 6 | 6615 | 12.533 s | `BUFX12H7L -> BUFX8H7L: 1`, `BUFX8H7L -> BUFX12H7L: 5` | setup 0.039 ns, hold -0.031 ns |
| 40ps | 0.0883 ns | 0.0800 ns | 0.0084 ns | false | +5.6000 um^2 | true | 5 | 5670 | 10.211 s | `BUFX8H7L -> BUFX12H7L: 5` | setup 0.039 ns, hold -0.031 ns |
| 0ps | 0.0883 ns | 0.0800 ns | 0.0084 ns | false | +5.6000 um^2 | true | 5 | 5670 | 10.091 s | `BUFX8H7L -> BUFX12H7L: 5` | setup 0.039 ns, hold -0.031 ns |

Additional summary fields from the logs:

| Target | Iterations | Rejected candidates | Cap rejections | Stop reason | Final buffer area | Total flow time |
|---:|---:|---:|---:|---|---:|---:|
| 80ps | 6 | 6596 | 0 | `no_improving_candidate` | 892.080 um^2 | 25.300 s |
| 40ps | 5 | 5652 | 0 | `no_improving_candidate` | 893.200 um^2 | 22.647 s |
| 0ps | 5 | 5652 | 0 | `no_improving_candidate` | 893.200 um^2 | 21.846 s |

## Interpretation

The optimization now evaluates candidate buffer master changes through fast STA incremental updates:

- candidate apply/revert uses `FastStaAdapter::changeBufferMaster(...)`;
- skew comes from `FastStaAdapter::querySkew(...)`;
- power/area comes from `FastStaAdapter::queryPower(...)`;
- cap legality comes from `FastStaAdapter::queryCapStatus(...)`;
- rejected trials are restored by changing the node back to the previous master and updating fast STA state.

There is no remaining `CharTimingLookup` or `TreeBufferSizing` dependency in `src/operation/iCTS`. The old char-backed `source/module/buffer_sizing` implementation and tests are removed.

The 40ps and 0ps runs converge to the same final state because both targets are below the legal fixed-topology floor found by this optimizer. In that unreachable-target mode the objective becomes minimum spread under legal sizing moves, so the same best legal assignment is selected.

The final iSTA-reported setup/hold skew differs from fast STA's modeled skew. That is expected for this task because ordinary iSTA and CTS fast STA intentionally use different timing口径: ordinary iSTA uses total root load for cell lookup and impulse-based net slew, while fast STA follows the OpenSTA-style DMP Ceff and per-load Elmore path.

## Result

The migrated optimization satisfies the fixed-topology, fast-STA-backed requirement:

- no self-computed delay/slew formulas in optimization;
- no topology changes;
- no accepted cap violations;
- area remains the primary cost/reporting metric;
- 80ps target is met;
- tighter 40ps/0ps pressure runs complete and report the reachable fixed-topology floor.
