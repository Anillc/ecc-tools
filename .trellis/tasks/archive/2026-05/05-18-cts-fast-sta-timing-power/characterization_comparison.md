# Characterization comparison report

## Migration status

CTS characterization timing and power sampling now uses the CTS fast STA path:

- `src/operation/iCTS/source/module/characterization/CharBuilderCircuit.cc`
  - builds fast STA characterization contexts through `FastStaAdapter::buildCharContext(...)`;
  - updates sample load through `FastStaAdapter::setCharLoad(...)`;
  - erases char contexts when the temporary characterization circuit is destroyed.
- `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc`
  - runs each timing/power sample through `FastStaAdapter::runCharSample(...)`.
- `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc`
  - no longer wraps the build with old iSTA char-only initialization calls.
- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc`
  - still uses `STAAdapter` for narrow technology-data extraction, such as pin cap and wire cap sampling. This remains initialization/data-forwarding work, not the timing engine for char samples.

## Fast STA vs OpenSTA

The fast STA char path uses the same timing engine validated in `opensta_alignment.md`:

```text
input slew + CTS RC/load
  -> OpenSTA-style Pi/Elmore reduction
  -> DMP Ceff
  -> Liberty delay/slew lookup
  -> per-load wire delay/load slew
  -> power and area query
```

This makes characterization consistent with the fast STA timing used by optimization.

## Fast STA vs ordinary iSTA口径

The important difference is the cell-delay load model:

| Topic | CTS fast STA/OpenSTA-style char | Ordinary iSTA path |
|---|---|---|
| Driver cell lookup load | DMP effective capacitance from reduced Pi | RC root total load capacitance |
| RC model | detailed CTS RC, downstream cap, Pi `C2/Rpi/C1`, per-load Elmore | RC tree root load, per-node Elmore/impulse |
| Cell delay/slew | Liberty table with `(input_slew, Ceff)` | Liberty table with `(input_slew, total_load)` |
| Load delay/slew | per-load DMP waveform/Elmore response | net delay plus impulse slew formula |
| Characterization dependency | fast STA-owned char context | full iSTA char-only circuit path |

The ordinary iSTA code confirms this口径:

- `src/operation/iSTA/source/module/sta/StaDelayPropagation.cc:210` queries cell delay from `rc_net->load(...)` or net load.
- `src/operation/iSTA/source/module/sta/StaSlewPropagation.cc:152` queries cell slew from the same total load.
- `src/operation/iSTA/source/module/delay/ElmoreDelayCalc.cc:1173` returns RC root load.
- `src/operation/iSTA/source/module/delay/ElmoreDelayCalc.cc:129` propagates net slew as `sqrt(input_slew^2 + impulse)`.

## Expected differences

The fast STA char result can differ from previous iSTA-backed char results when the routed RC is non-lumped:

- DMP Ceff can be smaller than total load, changing cell delay and output slew.
- Per-load Elmore and waveform-derived load slew preserve load-location differences that total-load cell lookup cannot express.
- Buffer sizing changes affect both the current buffer's driver timing and the parent net's input pin cap through fast STA incremental updates.

These differences are intentional. The parent requirement was to use OpenSTA-style CTS timing for char and optimization, not to preserve ordinary iSTA total-load behavior.

## Power comparison

The CTS fast STA power implementation follows the OpenSTA/iPA shared CTS-relevant formulas:

- clock activity density: `2 / period`;
- switching power: `0.5 * load_cap * V^2 * activity`;
- internal power: Liberty internal-power table energy times transition activity;
- leakage: Liberty leakage data;
- area: Liberty cell area.

The difference from full iPA is scope and data source. Fast STA computes CTS-local clock-tree power from its own load/timing model and does not build the full iPA graph. iPA switching power uses the iSTA graph net load (`getNetLoad()`), while fast STA uses the CTS fast STA load model used by timing.

## Validation evidence

- Fast STA/OpenSTA numeric alignment is recorded in `opensta_alignment.md`.
- `./bin/icts_test_database_adapter_fast_sta` passed all 7 focused tests.
- `cmake --build build --target icts_source_module_characterization -j 8` completed successfully.
- The production binary command ran successfully on `ics55_dev` after char and optimization migration, with logs recorded in `scripts/design/ics55_dev/.trellis_fast_sta_opt_*.log`.
