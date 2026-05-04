# Root Driver Compensation Flow Runtime and Delay Analysis

Date: 2026-05-03

## Experiment Scope

- Current implementation: direct root-driver compensation integrated in H-tree synthesis.
- Baseline: clean worktree at commit `ab6fc495dbbed2c0717ba1b925fb90f66dd46e31`, before the compensation changes.
- Design/run script: `scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl`.
- Baseline log:
  - `/tmp/ecc-tools-baseline.VGCfq5/scripts/design/ics55_dev/result/baseline_nocomp_run.log`
  - `/tmp/ecc-tools-baseline.VGCfq5/scripts/design/ics55_dev/result/cts/cts.log`
- Current compensation log:
  - `/home/liweiguo/project/ecc-tools/scripts/design/ics55_dev/result/root_driver_comp_release_run.log`
  - `/home/liweiguo/project/ecc-tools/scripts/design/ics55_dev/result/cts/cts.log`

Important caveat: the earlier current run under `/home/liweiguo/project/ecc-tools/build` was a Debug build and must not be compared against the Release baseline. The numbers below compare Release-to-Release.

## Release Runtime Comparison

| Metric | Baseline no compensation | Current direct compensation | Delta | Delta % |
|---|---:|---:|---:|---:|
| CTS total | 31.161 s | 38.985 s | +7.824 s | +25.11% |
| synthesis | 16.298 s | 16.998 s | +0.700 s | +4.30% |
| HTree build | 9.215 s | 9.993 s | +0.778 s | +8.44% |
| CharBuilder build | 8.646 s | 9.348 s | +0.702 s | +8.12% |
| read_data | 6.002 s | 11.576 s | +5.574 s | +92.87% |
| evaluation | 8.852 s | 10.401 s | +1.549 s | +17.50% |
| report | 1.733 s | 2.164 s | +0.431 s | +24.87% |
| `/usr/bin/time` real | 55.78 s | 64.39 s | +8.61 s | +15.44% |

The full-flow CTS total delta is an upper bound for the integrated branch, not a clean attribution to the direct compensation mechanism. Most of the one-shot delta is outside the direct lookup itself, especially `read_data`, which should not be causally affected by root-driver compensation.

The mechanism-internal synthesis compensation cost reported by the H-tree summary is:

- compensated candidate count: `79488`
- unique direct Liberty lookup count: `24`
- cache hits: `79464`
- cache hit rate: `99.9698%`
- root-driver direct lookup runtime: `0.4158 ms`

This is:

- `0.0004158 s / 16.298 s = 0.00255%` of baseline synthesis time
- `0.0004158 s / 31.161 s = 0.00133%` of baseline CTS total time

Evaluation instrumentation also adds a measurable char validation probe:

- direct runtime total: `0.0680 ms`
- char runtime total: `685.0368 ms`
- char/direct runtime ratio: `10072.737`

This validation probe is useful for equivalence checking, but it is not part of the minimal direct synthesis compensation cost.

## Selected H-Tree Delay Metrics

Current selected H-tree metrics:

- selected raw/uncompensated H-tree delay: `0.2449 ns`
- selected root-driver cell delay from synthesis compensation: `0.0902 ns`
- selected compensated H-tree delay: `0.3351 ns`

The compensated delay is exactly:

```text
0.2449 ns + 0.0902 ns = 0.3351 ns
```

The selected compensation uses `driven_cap_idx/cap_lattice` as the load source during H-tree synthesis.

## Equivalent Root-Driver STA Probe

Evaluation QoR probe at the final rooted/routed context reported:

- routed root-net load: `0.090496 pF`
- input slew: `0.0250 ns`
- direct delay: `0.114432 ns`
- char delay: `0.114430 ns`
- char-direct delay: `-0.000002 ns`
- mean abs delay delta: `1.841e-06 ns`

Equivalent-semantics root-driver delay error:

- absolute: `1.841e-06 ns = 0.001841 ps`
- relative to char delay: `0.00161%`

This validates that the direct Liberty query and the char-style evaluation probe are effectively equivalent for the root-driver cell semantics.

## Synthesis Lattice Load vs Final Routed Load

If the final H-tree raw delay is combined with the evaluation probe's routed-load root-driver char delay:

```text
0.2449 ns + 0.114430 ns = 0.359330 ns
```

The final reported H-tree compensated delay is:

```text
0.3351 ns
```

Difference:

- `0.3351 ns - 0.359330 ns = -0.024230 ns`
- absolute: `24.23 ps`
- relative to routed-load char-composed estimate: `6.74%`

This mismatch is from the load source difference, not from direct-vs-char Liberty semantic error:

- synthesis compensation load source: candidate `driven_cap_idx` converted through the load-cap lattice
- evaluation probe load source: final routed root-net load after embedding/routed RC context exists

For the root cell alone, synthesis lattice compensation delay (`0.0902 ns`) is lower than routed-load char/direct delay (`~0.11443 ns`) by `24.23 ps`, or about `21.17%` of the routed-load root-cell delay.

## End-to-End Arrival Sanity Probe

The evaluation probe also measured root-input to terminal leaf-buffer-output arrival delta distribution:

- root AT: `0.0449 ns`
- min: `0.4209 ns`
- max: `0.4863 ns`
- mean: `0.4570 ns`
- median: `0.4580 ns`

The implementation computes these stats from `leaf_arrival - root_arrival`; `Root AT` is printed for context. This is not the same abstraction as the selected H-tree char metric, because it includes the embedded/routed downstream H-tree network under final STA context. Still, as an end-to-end sanity check, selected compensated delay `0.3351 ns` is lower by:

| STA arrival reference | Delta | Relative |
|---|---:|---:|
| min delta `0.4209 ns` | `85.8 ps` | `20.38%` |
| mean delta `0.4570 ns` | `121.9 ps` | `26.67%` |
| median delta `0.4580 ns` | `122.9 ps` | `26.83%` |
| max delta `0.4863 ns` | `151.2 ps` | `31.09%` |

Use this only as a post-embedding distribution sanity signal, not as the direct root-driver compensation equivalence metric.

## Recommended Comparison Semantics

For the current direct compensation experiment, there are two meaningful comparison layers:

1. Final H-tree metric vs same abstract H-tree metric with evaluation routed-load root-driver replacement:
   - current direct-compensated metric: `0.3351 ns`
   - hybrid evaluation root-driver replacement: `0.2449 ns + 0.114430 ns = 0.359330 ns`
   - delta: `-24.23 ps`, `-6.74%`
   - meaning: isolates the load-source mismatch in the root-driver compensation while keeping downstream H-tree char semantics fixed.

2. Final H-tree metric vs literal post-embedding STA root-input-to-leaf-output propagation:
   - current direct-compensated metric: `0.3351 ns`
   - mean propagation: `0.4570 ns`
   - median propagation: `0.4580 ns`
   - mean delta: `-121.9 ps`, `-26.67%`
   - median delta: `-122.9 ps`, `-26.83%`
   - meaning: end-to-end sanity check, but it mixes H-tree char abstraction with routed downstream STA semantics.

## Selection-Aware Root-Closure Load Run

Latest scoped-cache rerun: 2026-05-03, after changing synthesis compensation to use a candidate-specific
root-closure physical load estimate instead of `entry.get_driven_cap_idx()`.

Important semantic correction: the STA comparison below is only
`h-tree root input pin -> h-tree leaf buffer output pin`. It is not the earlier terminal/cluster output
probe that produced the 0.4+ ns distribution.

### Implementation Semantics

- `entry.get_driven_cap_idx()` remains the H-tree composition/hash-join boundary state.
- Root-driver compensation resolves a physical root-closure load for each legal candidate after sink-load-region filtering and before per-depth/global best selection.
- The resolver materializes the candidate topology pattern, walks through pure-wire upper levels, stops at the first real root-net terminals, adds terminal pin cap, and estimates root-net wire cap with `Router::buildFluteTree`.
- `RootDriverCompensationContext::root_load_by_pattern` is cleared per candidate build because `PatternId` values are local to each `TopologyPatternLibrary`.

### Solution-Space Changes

| Metric | Old direct load source | Selection-aware root closure | Delta |
|---|---:|---:|---:|
| selected depth | `6` | `6` | `0` |
| selected-depth final frontier | `9855` | `9855` | `0` |
| strict feasible entries | `5256` | `5256` | `0` |
| compensated candidate entries | `79488` | `79488` | `0` |
| direct Liberty lookup misses | `24` | `26` | `+2` |
| direct Liberty lookup cache hits | `79464` | `79462` | `-2` |
| root-load resolutions | n/a | `51840` | new |
| root-load cache hits | n/a | `27648` | new |
| root-load failures | n/a | `0` | new |
| FLUTE root-load estimates | n/a | `51840` | new |
| fallback route estimates | n/a | `0` | new |
| root-load resolution runtime | n/a | `3124.1167 ms` | new |
| reported direct lookup runtime | `0.4158 ms` | `1.0457 ms` | `+0.6299 ms` |

The structural search space did not expand or shrink: frontier, feasible, and compensated-candidate counts are unchanged.
The scoring surface changed because candidates are scored with their estimated root physical load rather than the abstract
driven-cap state.

### Selected Optimal Solution

| Metric | Old direct load source | Selection-aware root closure | Delta |
|---|---:|---:|---:|
| selected topology pattern id | `444266` | `444266` | unchanged |
| selected level segment pattern ids | not reported | `65042,64841,24,28,5,4` | reported |
| root driven cap idx | `6` | `6` | unchanged composition state |
| synthesis root load used for root driver | `0.0600 pF` | `0.0905 pF` | `+0.0305 pF` |
| selected root-load bucket idx | n/a | `10` | new physical-load bucket |
| root-load terminal count | n/a | `4` | new |
| root-load terminal pin cap | n/a | `0.0160 pF` | new |
| root-load wire cap | n/a | `0.0745 pF` | new |
| root-load routed wirelength estimate | n/a | `566.9300 um` | new |
| root-load route estimator | n/a | `flute_clock_steiner_tree` | new |
| selected root-driver cell delay | `0.0902 ns` | `0.1144 ns` | `+24.2 ps` |
| raw H-tree char delay | `0.2449 ns` | `0.2449 ns` | unchanged |
| compensated H-tree delay | `0.3351 ns` | `0.3594 ns` | `+24.3 ps` |
| compensated H-tree power | `343.292 uW` | `343.308 uW` | `+0.016 uW` |

The final selected topology did not change in the scoped-cache rerun. The optimal metric changed because the same topology's
root-driver cell is now evaluated at the estimated physical root load, which matches the post-embedding routed load
`0.090496 pF` to report precision.

### Correct H-Tree Leaf-Output STA Comparison

Correct evaluation probe:

- Root input: `cts_flow_clk_0_clk_i_regular_root_buf/A`
- H-tree leaf output samples: `64`
- Leaf sample role: `htree_leaf_edge_buffer_output_name_pattern`
- Root AT: `0.0449 ns`
- Min/Max/Mean/Median propagation: `0.3267 / 0.3546 / 0.3378 / 0.3365 ns`

| STA reference | Old metric error (`0.3351 ns`) | New metric error (`0.3594 ns`) | Absolute-error change |
|---|---:|---:|---:|
| min `0.3267 ns` | `+8.4 ps` | `+32.7 ps` | worse by `24.3 ps` |
| max `0.3546 ns` | `-19.5 ps` | `+4.8 ps` | improves by `14.7 ps` |
| mean `0.3378 ns` | `-2.7 ps` | `+21.6 ps` | worse by `18.9 ps` |
| median `0.3365 ns` | `-1.4 ps` | `+22.9 ps` | worse by `21.5 ps` |

Interpretation:

- The root-load semantic bug is fixed: the synthesis root load moves from `0.0600 pF` to `0.0905 pF`, matching the evaluation routed root load `0.090496 pF`.
- The final H-tree leaf-output delay agreement improves against the worst/max leaf output, from `19.5 ps` low to `4.8 ps` high.
- The mean/median comparison becomes worse because the corrected root driver delay is added on top of the existing downstream H-tree char abstraction. That downstream abstraction is still not a routed STA distribution model.
- Therefore this patch should be described as fixing root-driver physical-load semantics and improving worst-leaf/max agreement, not as uniformly improving every STA leaf-output statistic.

### Runtime Delta

| Runtime metric | Old direct load source | Selection-aware root closure | Delta |
|---|---:|---:|---:|
| read_data | `11.576 s` | `39.635 s` | `+28.059 s` |
| synthesis | `16.998 s` | `73.383 s` | `+56.385 s` |
| evaluation | `10.401 s` | `24.547 s` | `+14.146 s` |
| total CTS | `38.985 s` | `137.627 s` | `+98.642 s` |

The one-shot flow runtime includes unrelated read/evaluation/report variability, machine load, and the temporary
char/evaluation/report probes. The compensation-internal cost is the cleaner attribution:

- root-closure physical load estimation: `3124.1167 ms` for `51840` FLUTE estimates;
- direct Liberty lookup: `1.0457 ms` for `26` unique lookups.

So the selection-aware mechanism adds about `3.125 s` of synthesis work in this run, dominated by root-load resolution,
not by Liberty lookup.

## Feasibility of Using Final Routed Root Load During Compensation

The current synthesis compensation cannot directly use the true final routed root-net capacitance, because compensation is applied during topology depth search before a final candidate is selected and before the H-tree is embedded/instantiated into real design objects.

Current ordering:

1. H-tree characterization and segment entry synthesis.
2. Candidate topology composition/search.
3. Root-driver compensation on every candidate using `entry.get_driven_cap_idx()` converted through the load-cap lattice.
4. Global candidate selection.
5. Embedding/instantiation creates actual root/output nets.
6. Evaluation installs routed RC and can query `STA_ADAPTER_INST.queryPinOutputNetLoad(root_output_pin)`.

For the current selected candidate:

- synthesis compensation load index: `root_driven_cap_idx = 6`
- cap lattice setup: `max_cap = 0.1500 pF`, `cap_steps = 15`, step `0.0100 pF`
- synthesis compensation load estimate: `6 * 0.0100 pF = 0.0600 pF`
- evaluation routed root-net load: `0.090496 pF`

This explains why the synthesis root-driver delay `0.0902 ns` is lower than the evaluation routed-load root-driver delay `0.114430 ns`.

## Root Cause of 0.0600 pF vs 0.090496 pF

The selected root output net is:

```text
cts_flow_clk_0_clk_i_regular_downstream_net
```

Its final netlist/DEF connectivity is:

- driver: `cts_flow_clk_0_clk_i_regular_root_buf/Y`
- loads:
  - `cts_flow_clk_0_clk_i_regular_htree_edge_buf_80/A`
  - `cts_flow_clk_0_clk_i_regular_htree_edge_buf_81/A`
  - `cts_flow_clk_0_clk_i_regular_htree_edge_buf_82/A`
  - `cts_flow_clk_0_clk_i_regular_htree_edge_buf_83/A`

So the root fanout is four first-level H-tree buffers, matching the intended selected topology. The mismatch is not caused by missing or extra driven pins.

The library input capacitance for `BUFX20H7L/A` in the ss_rcworst NLDM lib is:

```text
0.0039934 pF
```

For four loads:

```text
4 * 0.0039934 pF = 0.0159736 pF
```

The evaluation routed root-net load is:

```text
0.090496 pF
```

Therefore the routed wire-cap component is approximately:

```text
0.090496 pF - 0.0159736 pF = 0.0745224 pF
```

The synthesis compensation uses `entry.get_driven_cap_idx()` through the configured cap lattice:

```text
root_driven_cap_idx = 6
max_cap = 0.1500 pF
cap_steps = 15
step = 0.0100 pF
load = 0.0600 pF
```

If the same four pin caps are subtracted from the synthesis load, the synthesis model implies only:

```text
0.0600 pF - 0.0159736 pF = 0.0440264 pF
```

of wire-related load.

Using the MET5 tech LEF approximation:

```text
width = 0.1 um
CPERSQDIST = 0.0006259 pF / square
EDGECAPACITANCE = 0.0000344 pF / um
cap_per_um = 0.1 * 0.0006259 + 2 * 0.0000344 = 0.00013139 pF/um
```

Equivalent wire lengths are:

| Quantity | Cap | MET5-equivalent length |
|---|---:|---:|
| Routed wire cap | `0.0745224 pF` | `567.185 um` |
| Synthesis implied wire cap | `0.0440264 pF` | `335.082 um` |
| Shortfall | `0.0304960 pF` | `232.103 um` |

This aligns with the post-routing wirelength report:

```text
Max Net Length routed = 566.930 um
Max Net Length HPWL   = 428.276 um
```

The selected root/load DEF placements produce the same HPWL:

```text
root = (282998, 289638)
buf80 = (370663, 158802)
buf81 = (211306, 158802)
buf82 = (349960, 414119)
buf83 = (197704, 414119)
DBU = 1000 / um
HPWL = 428.276 um
sum root-to-load Manhattan = 822.247 um
```

Conclusion: the `0.0600 pF` load is a valid lattice-derived abstract boundary load for the selected char entry, but it is too small as a physical proxy for the final root output net. The underestimation is dominated by root-net wire capacitance:

- total load ratio: `0.090496 / 0.0600 = 1.508x`
- wire-only ratio: `0.0745224 / 0.0440264 = 1.693x`
- missing wire-cap component: `0.030496 pF`, about `232 um` MET5-equivalent length

The likely modeling gap is that the H-tree char/topology composition uses abstract level/segment load-cap state, while the embedded root output net is a 4-sink FLUTE/Steiner route over a large first-level H-tree bbox. The final routed root net is physically closer to the `566.930 um` routed max-net length than to the `335 um` wire length implied by the lattice load.

### Why Root Driven Cap Index Is 6 Instead of 9

The selected `root_driven_cap_idx = 6` is not computed from the final rooted net after embedding. It is the source-side boundary cap index carried by the selected H-tree characterization entry.

Generation chain:

1. CharBuilder samples a segment pattern and computes `driven_cap_pf`.
   - If the segment pattern has internal buffers:
     `driven_cap_pf = input_cap(first_buffer) + wire_cap(first_wire_segment)`.
   - If the segment pattern has no internal buffers:
     `driven_cap_pf = load_pf + sum(wire_cap(segment_wires))`.
2. CharBuilder stores:
   `driven_cap_idx = cap_lattice.coveringIndex(driven_cap_pf)`.
3. `SegmentChar::compose()` preserves the upstream/source-side driven cap:
   `merged.driven_cap_idx = upstream.driven_cap_idx`.
4. `MakeHTreeSeedEntries()` copies `segment_entry.get_driven_cap_idx()` into the H-tree seed entry.
5. `HTreeTopologyChar::compose()` also preserves the upstream/source-side driven cap:
   `merged.driven_cap_idx = upstream.driven_cap_idx`.
6. Root-driver compensation later uses that selected entry field directly:
   `load_cap_idx = entry.get_driven_cap_idx()`.

With the current cap lattice:

```text
max_cap = 0.1500 pF
cap_steps = 15
step = 0.0100 pF
```

`root_driven_cap_idx = 6` means the selected entry's char-stage source-side `driven_cap_pf` landed in the sixth 10 fF bin, i.e. approximately up to `0.0600 pF`. A value near the evaluation routed load, `0.090496 pF`, would be near index `9` if rounded to the nearest printed bin, and index `10` under the current `coveringIndex()` ceiling rule.

The current code has no step that says:

```text
root_driven_cap_idx = cap_lattice.coveringIndex(final_routed_root_net_load)
```

So index `6` is not selected over `9` by a routed-load comparison. It is inherited from the abstract selected topology entry. The final embedded root net can then connect through multiple unbuffered topology levels until the first real buffers, producing a multi-sink physical net whose routed RC load is larger than the abstract source-side boundary cap.

### Root Net FLUTE Routing Shape

Evaluation and visualization build routed CTS net trees through `Router::buildClockNetTree()`. For nets with more than one load, the implementation dispatches to `buildFluteTree()`, so the selected root output net's RC is based on FLUTE routing, not direct driver-to-load flylines.

The first six design-view SVG route segments match the selected root output net:

```text
root -> bottom_branch
bottom_branch -> buf80
bottom_branch -> buf81
root -> top_branch
top_branch -> buf82
top_branch -> buf83
```

In DEF DBU coordinates:

| Segment | From | To | Length |
|---|---:|---:|---:|
| root -> bottom_branch | `(282998, 289638)` | `(282998, 158802)` | `130.836 um` |
| bottom_branch -> buf80 | `(282998, 158802)` | `(370663, 158802)` | `87.665 um` |
| bottom_branch -> buf81 | `(282998, 158802)` | `(211306, 158802)` | `71.692 um` |
| root -> top_branch | `(282998, 289638)` | `(282998, 414119)` | `124.481 um` |
| top_branch -> buf82 | `(282998, 414119)` | `(349960, 414119)` | `66.962 um` |
| top_branch -> buf83 | `(282998, 414119)` | `(197704, 414119)` | `85.294 um` |
| Total | | | `566.930 um` |

This is an H-shaped routing tree centered at the root x-coordinate, not an obviously pathological detour.

The same net's bbox HPWL is:

```text
xspan = 370663 - 197704 = 172.959 um
yspan = 414119 - 158802 = 255.317 um
HPWL  = 428.276 um
```

So the FLUTE/H-shaped routed tree is:

```text
566.930 / 428.276 = 1.324x HPWL
```

Using the MET5 unit-cap approximation:

```text
cap_per_um = 0.00013139 pF/um
```

The route decomposes to approximately:

| Component | Length | Wire cap |
|---|---:|---:|
| root -> bottom_branch | `130.836 um` | `0.0171905 pF` |
| bottom_branch -> buf80 | `87.665 um` | `0.0115183 pF` |
| bottom_branch -> buf81 | `71.692 um` | `0.0094196 pF` |
| root -> top_branch | `124.481 um` | `0.0163556 pF` |
| top_branch -> buf82 | `66.962 um` | `0.0087981 pF` |
| top_branch -> buf83 | `85.294 um` | `0.0112068 pF` |
| Total wire | `566.930 um` | `0.0744889 pF` |

Adding four `BUFX20H7L/A` input caps:

```text
pin cap = 4 * 0.0039934 pF = 0.0159736 pF
wire + pin = 0.0744889 pF + 0.0159736 pF = 0.0904625 pF
```

This matches the evaluation STA load `0.090496 pF` within rounding/unit-query differences. Therefore the routed RC value is internally consistent with the FLUTE/H-shaped route.

Routing-vs-HPWL contributes a meaningful part of the gap:

```text
HPWL-based load ~= 0.0159736 + 428.276 * 0.00013139 = 0.0722448 pF
FLUTE load      ~= 0.090496 pF
delta           ~= 0.01825 pF
```

But `idx=6` corresponds to only `0.0600 pF`, which is lower than even this HPWL-based estimate. The full shortfall from synthesis to evaluation is:

```text
0.090496 - 0.060000 = 0.030496 pF
```

So the mismatch is not explained solely by FLUTE being longer than HPWL. The larger issue remains that root-driver compensation uses the abstract selected entry's source-side `driven_cap_idx`, not a physical estimate of the collapsed 4-sink root net's H-shaped route.

Viable fixes depend on the intended semantics:

1. Post-selection/report correction:
   - After evaluation, recompute selected root-driver direct compensation using `queryPinOutputNetLoad(root_output_pin)`.
   - Report a routed-load-corrected H-tree delay:
     `0.2449 ns + 0.114432 ns = 0.359332 ns`.
   - This is cheap and removes the root-load mismatch from the final reported metric, but it does not affect candidate selection.

2. Two-pass optimization:
   - Run synthesis once, instantiate/evaluate, measure routed root load, then feed the measured load into a second selection pass or local rerank.
   - This can make selection aware of a routed-load correction, but the measured cap belongs to the first selected topology; it is not a true routed cap for every unselected candidate.

3. Candidate-specific routed evaluation:
   - Instantiate/route/evaluate the top K candidates and rerank using each candidate's measured root load.
   - This is the closest to true semantics, but it is much heavier and requires careful rollback/cleanup of temporary candidates.

4. Pre-route load estimator:
   - Improve the synthesis-stage load estimate using candidate geometry and unit RC instead of only the cap lattice driven-cap index.
   - This can reduce the mismatch but is still an estimate, not the true routed cap.
