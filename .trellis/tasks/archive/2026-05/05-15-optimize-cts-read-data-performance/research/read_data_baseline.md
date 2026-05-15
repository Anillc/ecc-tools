# CTS read_data baseline notes

## Existing huge-case logs

Existing `design_clock.def` huge runs show:

- Native run `result_clock_native_huge_final`: `read_data` finished in 4.085 s for 129494 sinks.
- Analytical run `result_clock_analytic_huge_final`: `read_data` finished in 4.156 s for 129494 sinks.
- New DEF native run `result_newdef_native_huge_final`: `read_data` finished in 3.648 s for 122010 sinks.

For the native `design_clock.def` run:

- `CTSReadData` started at `09:48:14.452576`.
- SDC declaration-only read started at `09:48:14.452606` and ended at `09:48:14.453256`.
- `Clock Distribution Overview` started at `09:48:18.532627`.
- `CTSReadData` finished at `09:48:18.537853`.

This means SDC declaration reading is not the bottleneck; nearly all `read_data` time is after SDC declaration parsing and before clock distribution summary emission. The likely scope is `WRAPPER_INST.readClocks()` and its iDB-to-CTS object materialization.

## Code-path suspicion

`Wrapper::CtsClockReader::buildClockFromIdbNet()` builds one CTS pin per iDB clock-net pin. For each load it currently calls:

- `clock->add_load(cts_pin)`
- `cts_net->add_load(cts_pin)`

Both APIs perform a linear `std::ranges::find` over the accumulating load vector to preserve uniqueness. The iDB net pin collection already deduplicates pins before this loop, so these per-load scans are likely redundant and create O(N^2) behavior on a clock with O(100k) sinks.

Next step: add focused timing or run an optimized build to confirm the cost reduction.

## Probe with current default config

After temporarily switching `run_iCTS_dev.tcl` to `design_clock.def`, the default `cts_default_config.json` still maps `clock -> n194404`. On the current `design_clock.def`, that reads only 2 sinks and finishes `read_data` in 0.001 s, so it is not a valid large-clock performance probe.

For the large-clock probe, this task uses `research/design_clock_cts_config.json`, which maps `clock -> clock`, and passes it through `CTS_CONFIG=...` without changing the default CTS config.

## Optimization result

Command shape:

```bash
cd scripts/design/ics55_huge_dev
env RESULT_DIR=... TOOL_REPORT_DIR=... CTS_CONFIG=/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/05-15-optimize-cts-read-data-performance/research/design_clock_cts_config.json \
    /usr/bin/time -p timeout 120s ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Both runs were killed after `read_data` and early synthesis because final CTS completion is not needed for this task.

| Build | Input | Clock net | Sinks | read_data |
| --- | --- | --- | ---: | ---: |
| Before optimization | `design_clock.def` | `clock` | 129494 | 4.106 s |
| After optimization | `design_clock.def` | `clock` | 129494 | 0.337 s |

The observed speedup is about 12.2x for CTS `read_data` on this case.

The measured result confirms that the dominant cost was the per-load linear uniqueness scan in `Clock::add_load()` and `Net::add_load()` inside `Wrapper::CtsClockReader::buildClockFromIdbNet()`. The reader now accumulates the already-deduplicated CTS load vector and assigns it once to `Clock` and `Net`.

## Full native run with SDC-derived clock net

After the optimized `read_data` change, a full native run was executed with `design_clock.def` and `use_netlist=OFF`:

```bash
cd scripts/design/ics55_huge_dev
env INPUT_DEF=./design_clock.def \
    RESULT_DIR=./result_clock_native_sdc_full \
    TOOL_REPORT_DIR=./result_clock_native_sdc_full/cts \
    OUTPUT_DEF=./result_clock_native_sdc_full/iCTS_result.def \
    OUTPUT_VERILOG=./result_clock_native_sdc_full/iCTS_result.v \
    DESIGN_STAT_TEXT=./result_clock_native_sdc_full/report/cts_stat.rpt \
    DESIGN_STAT_JSON=./result_clock_native_sdc_full/report/cts_stat.json \
    TOOL_METRICS_JSON=./result_clock_native_sdc_full/metric/iCTS_metrics.json \
    CTS_CONFIG=/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/05-15-optimize-cts-read-data-performance/research/design_clock_sdc_config.json \
    /usr/bin/time -p timeout 240m ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The run resolved `clock` from SDC, not config netlist mappings:

- `use_netlist=false`
- `configured_clock_net_mappings=0`
- clock net `clock`
- total sinks `129494`

Runtime:

| Stage | Runtime |
| --- | ---: |
| read_data | 0.338 s |
| synthesis | 311.389 s |
| instantiation | 1.580 s |
| evaluation | 55.634 s |
| CTS total | 369.106 s |
| report | 5.026 s |
| script wall time | 416.63 s |

STA substage note:

- `StaSlewPropagation` started at `14:33:08.988567`.
- `StaSlewPropagation` ended at `14:33:12.296940`.
- `update timing` elapsed time was `19.5306s`.

By contrast, the preceding `use_netlist=ON` run with `clock -> clock` reached `StaSlewPropagation` and was manually terminated after `real 1977.78s` without completing evaluation. This strongly suggests the STA stall is tied to the `use_netlist` path or to state produced by that path, even though both runs read the same clock name and sink count.
