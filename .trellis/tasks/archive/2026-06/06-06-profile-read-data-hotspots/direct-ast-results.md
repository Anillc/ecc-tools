# Direct Raw-AST Semantic Interface Results

## Scope

Implemented a first landable direct semantic interface for the iSTA Liberty
load path:

```text
Liberty text
  -> current C++ raw AST
  -> LibertyReader direct C++ AST visitor
  -> iSTA semantic Liberty model
```

This removes the iSTA hot path dependency on:

```text
liberty_convert_raw_group_stmt
  -> C wrapper statement/value objects
  -> wrapper-based LibertyReader traversal
```

The public raw C parser API remains available and unchanged for compatibility
users.

## Verification

Build:

```bash
ninja -C build iEDA
```

Binary run:

```bash
cd scripts/design/ics55_dev
/usr/bin/time -v ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Artifacts:

```text
.trellis/tasks/06-06-profile-read-data-hotspots/artifacts/direct_ast/
```

QoR check:

```bash
cmp current_perf/iCTS_metrics.json direct_ast/iCTS_metrics.json
```

Result: exact byte-for-byte match.

Per user constraint, no iSTA `ecc_dev_tools` check was run.

## Runtime Result

| Bucket | Baseline | Direct AST | Delta | Improvement |
| --- | ---: | ---: | ---: | ---: |
| Liberty raw load/parse | 5.623 s | 5.751 s | +0.128 s | -2.3% |
| Liberty link/object construction | 4.723 s | 3.124 s | -1.599 s | 33.9% |
| Post-Liberty read-data work | 0.141 s | 0.139 s | -0.002 s | 1.4% |
| `read_data` | 10.488 s | 9.014 s | -1.474 s | 14.1% |
| CTS total | 21.626 s | 20.153 s | -1.473 s | 6.8% |
| Process wall time | 25.08 s | 23.01 s | -2.07 s | 8.3% |

Peak memory:

| Metric | Baseline | Direct AST | Delta |
| --- | ---: | ---: | ---: |
| read_data peak VMem delta | 1668.920 MB | 1469.488 MB | -199.432 MB |
| CTS total peak VMem delta | 5111.340 MB | 4911.912 MB | -199.428 MB |
| `/usr/bin/time` max RSS | 5474528 KB | 5274860 KB | -199668 KB |

Liberty timestamp split from the direct AST run:

| Library | Raw load | Direct link |
| --- | ---: | ---: |
| H7CL | 2.881653 s | 1.564139 s |
| H7CR | 2.869405 s | 1.560226 s |
| Total | 5.751058 s | 3.124365 s |

## Conclusion

The new direct raw-AST semantic interface is binary-feasible and preserves the
observed iCTS output contract for this workload. The measured speedup is almost
entirely in Liberty link/object construction, which confirms the previous
runtime bottleneck analysis: C wrapper allocation/traversal and table value
conversion were material overhead in `CTSReadData`.

Remaining optimization potential is now concentrated in raw Liberty scanning
and AST construction. The next engineering step toward the PRD target is to
replace the raw-AST hop with a parser event sink for the iSTA path, while
keeping the raw AST sink for compatibility APIs.
