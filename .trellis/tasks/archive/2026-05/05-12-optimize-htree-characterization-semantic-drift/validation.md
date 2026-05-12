# Validation Summary

## Commands

```bash
ninja -C build iEDA
ninja -C build icts_test_flow_synthesis_htree
./bin/icts_test_flow_synthesis_htree --gtest_filter=HTreeTest.*
ninja -C build iEDA icts_test_flow_synthesis
./bin/icts_test_flow_synthesis
git diff --check
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Binary validation used the required flow:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The binary flow was run twice using the same local `scripts/design/ics55_dev/iEDA_config/cts_default_config.json` flow configuration:

- `root_input_slew = 0.0`, `max_fanout = 4`, `wirelength_iterations = 3`, `slew_steps = 10`, `cap_steps = 10`
- `root_input_slew = 0.0`, `max_fanout = 32`, `wirelength_iterations = 3`, `slew_steps = 10`, `cap_steps = 10`

After validation, the local config was restored to `max_fanout = 4`.

## Quality Gate

- `ninja -C build iEDA`: passed.
- `./bin/icts_test_flow_synthesis_htree --gtest_filter=HTreeTest.*`: passed, 8 tests.
- `./bin/icts_test_flow_synthesis`: passed, 14 tests.
- `git diff --check`: passed.
- `ecc_dev_tools`: passed with `In-scope findings: 0`.
- `ecc_dev_tools` out-of-scope findings: `4930`.
- `ecc_dev_tools` total runtime: `560.853s`.

## Binary Metrics

| Metric | Fanout 4 | Fanout 32 |
|---|---:|---:|
| Flow status | finished | finished |
| `/usr/bin/time` real | 95.29s | 50.80s |
| `/usr/bin/time` user | 122.27s | 79.96s |
| `/usr/bin/time` sys | 10.88s | 9.60s |
| CTS API elapsed | 72.513s | 28.926s |
| CTS synthesis elapsed | 54.851s | 12.072s |
| CTS evaluation elapsed | 8.366s | 7.531s |
| Selected H-tree depth | 11 | 5 |
| H-tree inserted buffers | 1381 | 36 |
| Final clock buffers | 4392 | 356 |
| Final buffer area | 12592.160 um^2 | 1013.600 um^2 |
| Total clock network wirelength | 59190.091 um | 43236.437 um |
| Max clock net wirelength | 525.328 um | 566.235 um |
| Setup WNS / TNS | 7.307 ns / 0.000 ns | 7.303 ns / 0.000 ns |
| Hold WNS / TNS | 0.028 ns / 0.000 ns | 0.004 ns / 0.000 ns |
| Setup worst skew / avg worst skew | 0.054 ns / 0.052 ns | 0.055 ns / 0.055 ns |
| Hold worst skew / avg worst skew | -0.052 ns / -0.050 ns | -0.046 ns / -0.039 ns |
| Root input slew | 0.0000 ns | 0.0000 ns |
| Root output slew | 0.0694 ns | 0.0632 ns |
| Root cap bucket delta | 0 | 0 |
| Root slew bucket delta | 0 | 0 |
| Raw top input slew constraint idx | none | none |
| Source-trunk min input slew idx | none | none |
| Boundary fallback used | false | false |
| Max fanout observed in setup skew report | 4 | 32 |
| Max fanout observed in hold skew report | 4 | 32 |

## Evaluation STA Error

| Metric | Fanout 4 | Fanout 32 |
|---|---:|---:|
| Selected compensated char delay | 0.6165 ns | 0.4188 ns |
| Raw H-tree char delay | 0.5155 ns | 0.3243 ns |
| Root-driver compensation delay | 0.1010 ns | 0.0945 ns |
| STA root-input to H-tree leaf-buffer output min | 0.4417 ns | 0.3467 ns |
| STA root-input to H-tree leaf-buffer output max | 0.5274 ns | 0.4178 ns |
| STA root-input to H-tree leaf-buffer output mean | 0.4913 ns | 0.3675 ns |
| STA root-input to H-tree leaf-buffer output median | 0.5005 ns | 0.3641 ns |
| Error vs STA mean | 0.1252 ns | 0.0513 ns |
| Error vs STA mean ratio | 25.48 % | 13.96 % |
| Error vs STA median | 0.1160 ns | 0.0547 ns |

The error is computed as `selected compensated char delay - evaluation STA root-input-to-leaf-output delay`.

## Notes

- `root_input_slew = 0.0` is now visible in the runtime config report.
- `raw_top_input_slew_constraint_idx = none` confirms the explicit `0.0` value is not converted into a lower-bound slew lattice filter.
- `SourceTrunkSegment` reports `min_input_slew = 0.0000 ns` and `min_input_slew_idx = none`, matching the ideal/no-lower-bound semantics.
- Root boundary closure matched both cap and slew buckets in the selected solutions (`root_cap_bucket_delta = 0`, `root_slew_bucket_delta = 0`).
