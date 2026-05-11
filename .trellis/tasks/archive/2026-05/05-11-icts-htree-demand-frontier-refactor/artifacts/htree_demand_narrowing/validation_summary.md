# HTree Demand Narrowing Validation

Command:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Baseline:

- `.trellis/tasks/archive/2026-05/05-11-icts-runtime-optimization/artifacts/source_trunk_all_frontier/`

Current:

- `.trellis/tasks/05-11-icts-htree-demand-frontier-refactor/artifacts/htree_demand_narrowing/`

## Runtime Comparison

| Metric | Baseline | Current | Result |
| --- | ---: | ---: | --- |
| HTree segment frontier entries | 73352 | 36676 | -50.0% |
| HTree segment frontier synthesis | 0.326 s | 0.196 s | -39.9% |
| HTree build | 18.450 s | 16.970 s | -8.0% |
| Downstream HTree topology stage | 18.846 s | 17.286 s | -8.3% |
| CTS internal total | 43.511 s | 41.129 s | -5.5% |
| Process wall time | 65.32 s | 62.44 s | -4.4% |
| CTS peak vmem delta | 5473.296 MB | 5145.620 MB | -6.0% |

## QoR / Physical Comparison

| Metric | Baseline | Current | Result |
| --- | ---: | ---: | --- |
| selected depth | 5 | 5 | same |
| selected HTree levels | 5 | 5 | same |
| HTree inserted buffers | 40 | 40 | same |
| final clock buffers | 360 | 360 | same |
| final buffer area | 1038.240 um^2 | 1038.240 um^2 | same |
| max clock net wirelength | 843.356 um | 843.356 um | same |
| total clock network wirelength | 43151.203 um | 43151.203 um | same |
| setup WNS | 7.302292 ns | 7.302292 ns | same |
| hold WNS | 0.008315 ns | 0.008315 ns | same |
| raw HTree metric | 0.2897 ns / 192.458 uW | 0.2897 ns / 192.458 uW | same |
| compensated HTree metric | 0.4959 ns / 217.271 uW | 0.4959 ns / 217.271 uW | same |
| selected root driver | BUFX20H7L | BUFX20H7L | same |
| selected physical root load | 0.1428 pF | 0.1428 pF | same |
| terminal branch-buffered levels | 0/5 | 0/5 | same |

## Pattern ID Note

The selected segment pattern IDs changed from `522717,468375,28,24,6` to `293967,239625,28,24,6`.
This is expected internal pattern-id drift after skipping unused branch/leaf segment frontiers in unrestricted HTree demand.
The materialized QoR and final physical metrics above are unchanged.
