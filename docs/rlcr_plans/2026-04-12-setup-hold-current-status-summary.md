# 2026-04-12 Setup/Hold Current Status Summary

## Scope

This note summarizes the current iSTA setup/hold alignment state for
`NV_NVDLA_partition_m`, after the exact check-pair preservation work and the
subsequent exporter-side cleanup.

The comparison pin used for focused validation is:

- port: `sc2mac_dat_pd[7]`
- related clock: `nvdla_core_clk`
- endpoint: `u_NV_NVDLA_cmac_u_core_u_rt_in_in_rt_dat_pd_d1_reg[7]:D`

## Changes Kept In This Stage

The current code state keeps the following fixes:

- preserve exact check-pair provenance on the iSTA side and propagate it into
  sequential path data
- convert check lookup second-axis time input from `ns` into the owning
  liberty time unit when calling `LibArc::getDelayOrConstrainCheckNs(...)`
- for exporter-side `reference_check_arc` fallback, use the exported
  `input_trans_type` to choose the `rise_constraint` / `fall_constraint`
  bucket, instead of reusing `data_end_trans_type`
- when `preserved_seq_match_status=local_binding`, do not directly trust the
  local preserved scalar margin as the primary source; prefer re-lookup from
  `reference_check_arc`

## Current Scalar Comparison

Focused comparison against OpenROAD for `sc2mac_dat_pd[7]`:

| item | iEDA current (ps) | OpenROAD (ps) | delta (ps) |
| --- | ---: | ---: | ---: |
| hold_rise | 6.64400 | 1.42987 | +5.21413 |
| hold_fall | -11.12600 | -17.22117 | +6.09517 |
| setup_rise | 14.99450 | 17.95644 | -2.96194 |
| setup_fall | 9.95612 | 33.10923 | -23.15311 |

Interpretation:

- `setup_rise` is now close and falls within the current test tolerance.
- `hold_rise` / `hold_fall` are close in scale but still outside tolerance.
- `setup_fall` remains the dominant mismatch.

## What Was Confirmed

### 1. `setup_rise` had an exporter bucket-selection bug

For the `reference_check_arc` fallback path, using `data_end_trans_type`
caused the exporter to query the wrong setup/hold constraint bucket.

Switching that selection to the exported `input_trans_type` materially improved
`setup_rise`:

- before: `6.35284312 ps`
- after: `14.99450 ps`
- OpenROAD: `17.95644 ps`

This fix is kept.

### 2. `clock_arrival_delta_ns = 0` is not the main issue here

For this ideal-clock case, plain full-STA clock arrival of `0` is acceptable.
The remaining error is not explained by clock latency handling.

### 3. `setup_fall` is blocked by missing preserved max/setup pair coverage

Tracing the preserved sequence snapshots for endpoint
`u_NV_NVDLA_cmac_u_core_u_rt_in_in_rt_dat_pd_d1_reg[7]:D` showed:

- `analysis_mode=min` snapshots do include `data_start_vertex=sc2mac_dat_pd[7]`
- `analysis_mode=max` snapshots do **not** include
  `data_start_vertex=sc2mac_dat_pd[7]`

Instead, the preserved max/setup winner is from another startpoint, e.g.
`u_NV_NVDLA_cmac_u_core_u_rt_in_in_rt_dat_pvld_d1_reg:CLK`, plus a few
binding-only records with different provenance.

This means the current exporter is not missing a simple arithmetic term for
`setup_fall`; it is missing the exact full-STA preserved source pair for this
port/clock/check combination.

## Current Conclusion

The current implementation style is near its natural limit:

- small exporter-side fixes can still improve cases like `setup_rise`
- but cases like `setup_fall` will not reliably converge as long as full-STA
  only preserves the active winner for max/setup and does not retain the exact
  port-to-check pair needed by timing-model export

So the remaining gap is no longer mainly an exporter formula problem.
It is primarily a provenance coverage problem in preserved full-STA setup data.

## Recommended Next Step

If the goal is only:

- every scalar has a source
- the model exports cleanly
- values are broadly comparable

then the current stage is acceptable as a checkpoint.

If the goal is stronger alignment with OpenROAD, the next change should not be
another exporter-only tweak. The next step should be:

- preserve all export-required exact input/check pairs during STA, not only the
  final max/setup winner

Without that, `setup_fall`-style gaps are expected to remain.

## Verification Snapshot

Fresh verification on this checkpoint:

- `./bin/iSTATest --gtest_filter=LibertyAlignmentTest.setup_hold_constraint_sources_are_dumped_for_all_scalar_entries`
  - result: pass
- `./bin/iSTATest --gtest_filter=LibertyAlignmentTest.full_sta_preserved_seq_data_covers_all_sc2mac_port_clock_pairs`
  - result: pass
- `./bin/iSTATest --gtest_filter=LibertyAlignmentTest.setup_hold_constraint_values_track_openroad_reference_scale`
  - result: fail
  - remaining mismatches:
    - `hold_rise`: iEDA `6.64400 ps`, OpenROAD `1.42987 ps`
    - `hold_fall`: iEDA `-11.12600 ps`, OpenROAD `-17.22117 ps`
    - `setup_fall`: iEDA `9.95612 ps`, OpenROAD `33.10923 ps`

So this commit should be treated as a checkpoint that improves provenance and
export behavior, but does not yet fully close scalar numeric alignment.
