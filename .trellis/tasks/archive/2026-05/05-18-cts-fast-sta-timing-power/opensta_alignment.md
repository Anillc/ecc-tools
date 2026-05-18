# OpenSTA alignment report

## Source revisions

- OpenROAD: `5894d0639dfab272652582b4e15185b68ea8a868`
- OpenSTA submodule `src/sta`: `76c4d6df3537ccce331b5caa812196c3330ba7c4`
- OpenSTA binary: `/home/liweiguo/project/OpenROAD/build-opensta-codex/sta`

The OpenROAD checkout was refreshed with:

```bash
cd /home/liweiguo/project/OpenROAD
git pull --recurse-submodules
git submodule update --init --recursive
```

## Reference model

The implemented CTS fast STA timing model follows the OpenSTA `dmp_ceff_elmore` path:

- `src/sta/dcalc/DelayCalc.cc:46` registers `dmp_ceff_elmore`.
- `src/sta/search/Sta.cc:396` makes it the default delay calculator.
- `src/sta/parasitics/ReduceParasitics.cc:108` reduces detailed parasitics to a Pi model.
- `src/sta/parasitics/ReduceParasitics.cc:323` stores the reduced Pi plus per-load Elmore values.
- `src/sta/dcalc/DmpCeff.cc:1018` computes DMP effective capacitance and driver timing.
- `src/sta/dcalc/DmpDelayCalc.cc:112` computes per-load wire delay and load slew.
- `src/sta/dcalc/GraphDelayCalc.cc:1046` annotates gate delay, wire delay, driver slew, and load slew into the graph.
- `src/sta/power/Power.cc:1077`, `:1417`, and `:1559` are the CTS-relevant power references for instance power, switching power, and clock activity.

## Micro-case

The deterministic validation case is under:

```text
.trellis/tasks/05-18-cts-fast-sta-timing-power/opensta_alignment/
```

It defines a small Liberty/Verilog/Tcl setup with two buffers and annotated Pi/Elmore parasitics. The OpenSTA command used was:

```bash
cd /home/liweiguo/project/ecc-tools/.trellis/tasks/05-18-cts-fast-sta-timing-power/opensta_alignment
/home/liweiguo/project/OpenROAD/build-opensta-codex/sta -no_init -exit cts_fast_sta_alignment.tcl
```

OpenSTA emitted warning 441 about `set_input_delay` relative to a clock on the same port. That warning is benign for this micro-case because the comparison target is `report_dcalc`, propagated clock-network delay, and power terms, not a user-facing constrained endpoint.

## Numeric alignment

OpenSTA `report_dcalc` for `u_buf`:

| Term | OpenSTA |
|---|---:|
| Pi C2 | 0.200000 |
| Pi Rpi | 1.000000 |
| Pi C1 | 0.800000 |
| Ceff | 0.433933 |
| Cell delay | 0.153393 ns |
| Table slew | 0.253393 ns |
| Driver waveform slew | 0.265526 ns |

OpenSTA `report_dcalc` for `u_leaf`:

| Term | OpenSTA |
|---|---:|
| Pi C2 | 0.100000 |
| Pi Rpi | 1.000000 |
| Pi C1 | 0.400000 |
| Ceff | 0.290294 |
| Cell delay | 0.146927 ns |
| Table slew | 0.246927 ns |
| Driver waveform slew | 0.254853 ns |

OpenSTA propagated clock-network delay to the sink was `0.522888 ns`.

The fast STA unit test `TimingPropagationMatchesOpenStaTwoLevelPath` checks the same propagation values:

| Point | Fast STA |
|---|---:|
| `u_buf/A` arrival | 0.000000000 ns |
| `u_buf/A` slew | 0.199999988 ns |
| `u_buf/Y` arrival | 0.153393298 ns |
| `u_buf/Y` slew | 0.265526026 ns |
| `u_leaf/A` arrival | 0.296730995 ns |
| `u_leaf/A` slew | 0.278976977 ns |
| `u_leaf/Y` arrival | 0.443657875 ns |
| `u_leaf/Y` slew | 0.254852772 ns |
| sink arrival | 0.522887707 ns |
| sink slew | 0.257477403 ns |

The unit tests also check the DMP driver terms:

| Term | Fast STA |
|---|---:|
| `u_buf` Ceff | 0.433933 |
| `u_buf` gate delay | 0.153393 |
| `u_buf` driver slew | 0.265526 |
| `u_buf` load wire delay | 0.143337 |
| `u_buf` load slew | 0.278977 |

OpenSTA `report_power -format json` for the micro-case reported:

| Clock power term | OpenSTA |
|---|---:|
| internal | `2.35579530e-04` |
| switching | `0.00000000e+00` |
| leakage | `2.00000013e-05` |
| total | `2.55579536e-04` |

The fast STA power path uses the same CTS-relevant equations: clock activity `2 / period`, switching `0.5 * C * V^2 * activity`, internal table energy times activity, leakage from Liberty leakage data, and area from Liberty cell area.

## Local validation evidence

The following targets were built successfully during this task:

```bash
cmake --build build --target icts_source_database_adapter_fast_sta -j 8
cmake --build build --target icts_source_module_characterization -j 8
cmake --build build --target icts_test_database_adapter_fast_sta -j 8
cmake --build build --target iEDA -j 8
```

The focused fast STA test binary passed:

```bash
./bin/icts_test_database_adapter_fast_sta
```

It ran 7 tests:

- `LibertyTableBilinearLookupInterpolates`
- `DmpDriverTimingProducesCeffAndLoadSlew`
- `DmpDriverTimingMatchesOpenStaMicroCase`
- `TimingPropagationMatchesOpenStaTwoLevelPath`
- `PiElmoreReductionPropagatesDownstreamCapAndElmore`
- `TimingPowerAndMasterChangeUpdateContext`
- `IncrementalMasterChangeMatchesFullRecompute`

## Conclusion

For the CTS subset in scope, the fast STA implementation aligns with OpenSTA `dmp_ceff_elmore` on Pi reduction, DMP Ceff, Liberty cell delay/slew lookup, per-load delay/slew propagation, sink arrival, and CTS-relevant power equations. Unsupported full-STA features such as current-source timing, SI, OCV, and arbitrary data-path checks remain out of scope by design.
