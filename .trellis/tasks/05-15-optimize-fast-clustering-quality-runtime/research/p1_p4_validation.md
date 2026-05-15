# P1-P4 FastClustering Validation

## Scope

Validation used the huge CTS case:

- workspace: `scripts/design/ics55_huge_dev`
- input DEF: `design_clock.def`
- clock: `clock`
- valid sink count: `129494`
- config base: `research/huge_design_clock_sdc_config.json`
- `use_netlist=OFF`
- native H-tree mode

The baseline and P1-P4 runs used the same SDC-driven clock source and PDK paths. Fanout sweep runs changed only `max_fanout`.

## Implemented Steps

### P1: Bounded Neighbor Selection and Aggregate Reuse

Changes:

- Replaced full candidate sorting in `SelectNearestActiveNeighbors()` with bounded top-K insertion.
- Avoided recomputing `CalcDraftAggregate()` for every boundary source; compute once per round and update the total routing-cap proxy after accepted moves.

Result:

- Quality intentionally unchanged.
- FastClustering runtime dropped from `234.798s` to `57.969s`.
- Final cluster count stayed `44962`, average fanout stayed `2.880`.
- Full CTS completed successfully.

### P2: Spatial Neighbor Graph

Changes:

- Built a spatial bucket neighbor graph once per merge/boundary polish round.
- Merge and boundary polish use graph-local neighbors, with bounded all-scan fallback if graph lookup is empty.

Result:

- FastClustering runtime dropped from `57.969s` to `6.132s`.
- Final cluster count stayed effectively unchanged at `44962`, average fanout stayed `2.880`.
- Synthesis completed in `98.644s`.
- This intermediate run was manually stopped in evaluation after it remained in `StaSlewPropagation`; this was recorded as an intermediate solver-risk signal, not a clustering legality failure.

### P3: Utilization-Aware Partitioning

Changes:

- Added fanout-scaled split candidate window.
- Added a normalized utilization penalty to split scoring so geometry/proxy improvements must pay for wasted cluster capacity.

Result:

- F4 recursive partition changed from `45316` drafts / `2.858` average fanout to `32379` drafts / `3.999` average fanout.
- Final F4 clusters were `32379`, all `129494` loads assigned, average fanout `3.999`.
- Synthesis completed in `63.397s`.
- This intermediate run was manually stopped in evaluation after it remained in `StaSlewPropagation`; P4 later completed the same F4 full flow successfully.

### P4: Selective Merge and Boundary Polish

Changes:

- Merge polish now attempts only singleton or under-utilized clusters below about 75% of fanout capacity.
- Boundary polish now selects only cap-heavy source clusters above `mean + 0.25 * stddev`, capped to 35% of active sources.
- Boundary polish stops early when the accepted move count is below the configured minimum.

Result:

- F4 merge attempts dropped from `32379` in P3 to `4`.
- F4 boundary considered sources dropped from `32379` per round in P3 to `9371` for one P4 round.
- F4 FastClustering runtime dropped from `3.311s` in P3 to `2.207s` in P4.
- F4 full CTS completed successfully.

## F4 Step-by-Step Results

| Run | Cluster Count | Avg Fanout | FastClustering | Prepare Sink Loads | Downstream HTree | Synthesis | Evaluation | CTS Total | Script Wall | Full CTS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Baseline | 44962 | 2.880 | 234.798s | 234.845s | 20.258s | 313.213s | 55.361s | 370.697s | 418.45s | yes |
| P1 | 44962 | 2.880 | 57.969s | 58.029s | 23.487s | 150.925s | 70.011s | 223.857s | 276.32s | yes |
| P2 | 44962 | 2.880 | 6.132s | 6.195s | 22.522s | 98.644s | stopped in STA | n/a | n/a | no |
| P3 | 32379 | 3.999 | 3.311s | 3.349s | 12.953s | 63.397s | stopped in STA | n/a | n/a | no |
| P4 | 32379 | 3.999 | 2.207s | 2.243s | 12.664s | 60.243s | 57.725s | 120.177s | 169.78s | yes |

## P4 Fanout Sweep

| max_fanout | Cluster Count | Avg Fanout | FastClustering | Prepare Sink Loads | Downstream HTree | Synthesis | Evaluation | CTS Total | Script Wall | Full CTS |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 4 | 32379 | 3.999 | 2.207s | 2.243s | 12.664s | 60.243s | 57.725s | 120.177s | 169.78s | yes |
| 8 | 16192 | 7.997 | 2.253s | 2.280s | 8.038s | 47.416s | 77.734s | 126.995s | 185.63s | yes |
| 16 | 8099 | 15.989 | 2.734s | 2.748s | 5.312s | 36.726s | stopped in STA | n/a | n/a | no |
| 32 | 4064 | 31.864 | 3.100s | 3.112s | 4.524s | 30.819s | 52.561s | 84.848s | 133.92s | yes |
| 64 | 2050 | 63.168 | 3.842s | 3.858s | 4.669s | 33.758s | 56.668s | 92.151s | 140.39s | yes |

## Fanout Distribution Evidence

P4 final fanout histograms:

- F4: `2=3,3=16,4=32360`
- F8: `4=1,5=2,6=6,7=20,8=16163`
- F16: `10=1,11=1,13=1,14=7,15=62,16=8027`
- F32: `25=2,26=3,27=5,28=18,29=26,30=45,31=257,32=3708`
- F64: `39=1,41=1,45=1,46=1,47=1,49=1,51=1,52=1,54=2,55=3,56=5,57=16,58=23,59=8,60=61,61=87,62=138,63=406,64=1293`

This confirms the utilization-aware split policy generalizes across small and large fanout values. As fanout increases, average fanout remains close to the configured limit and cluster count scales roughly with `ceil(loads / max_fanout)`.

## Solver Behavior Notes

- P4 F4/F8/F32/F64 completed full CTS, including instantiation, evaluation, DEF/netlist output, statistics, visualization, and GDS reports.
- P4 F16 completed clustering, H-tree, source-trunk, and synthesis, but the validation run was manually stopped in evaluation after `StaSlewPropagation` remained active for more than two minutes. The stack was in `ista::StaSlewPropagation::operator() -> Sta::updateTiming -> QorEvaluation::evaluate`.
- The F16 stop did not indicate a clustering legality failure: synthesis was finished, H-tree selection was legal, and `assigned_loads=129494`.
- Because F4/F8/F32/F64 completed full evaluation, the F16 stall is treated as an STA/evaluation sensitivity for this topology, not as evidence that P4 clustering is invalid.

## Correctness Checks From Logs

All successful and stopped P4 sweep runs reported:

- `assigned_loads=129494`
- cluster fanout at or below configured `max_fanout`
- downstream H-tree selected legal topology with `failure_reason=none`
- no fallback was needed for selected H-tree legality

The final exact legality gate remains in `FinalizeClusters()`, so draft-phase utilization and neighbor-search changes do not bypass fanout, diameter, or cap checks.

## Key Takeaways

1. The original runtime bottleneck was polish, not partition or finalize.
2. P1 and P2 removed most polish cost while preserving behavior.
3. The low F4 average fanout was caused by partition scoring, not final repair or diameter constraints.
4. P3 fixed packing by making utilization an explicit split objective.
5. P4 reduced unnecessary post-partition polish work and made the optimized result stable enough for full F4/F8/F32/F64 CTS.
6. The remaining risk is evaluation-time sensitivity for some topology/fanout combinations, especially the observed F16 run.
