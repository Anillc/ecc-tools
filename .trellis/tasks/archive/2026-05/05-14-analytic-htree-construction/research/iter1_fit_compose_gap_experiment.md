# Research: iter-1 fit and composed frontier gap experiment

- Scope: small real-tech experiment
- Date: 2026-05-14
- Test command:

```bash
cmake -S . -B build -DICTS_BUILD_SLOW_REALTECH_TESTS=ON
cmake --build build --target icts_test_module_characterization_realtech_regression -j 8
./bin/icts_test_module_characterization_realtech_regression --gtest_filter=CharacterizationRealTechExactRegressionTest.IterOneFitAndComposedFrontierGapReport
```

## Experiment Setup

The experiment builds real-tech segment characterization for three length iterations with:

```text
wirelength_unit_um = 25.0
wirelength_iterations = 3
raw_segment_char_count = 19050
```

It then:

1. Fits iter-1 samples grouped by segment pattern over `(input_slew_idx, load_cap_idx)`.
2. Builds direct frontiers for `length_idx = 1, 2, 3`.
3. Uses only the `length_idx = 1` frontier as the base and synthesizes `length_idx = 2, 3` by exact composition.
4. Compares composed frontiers with direct frontiers by materialized pattern signature and boundary buckets.

The full report is written by the test to:

```text
bin/icts_test_output/characterization/realtech/iter1_fit_compose_gap/iter1_fit_compose_gap_report.txt
```

## Iter-1 Fit Result

Iter-1 has:

```text
raw samples = 1095
pattern groups = 5
```

Key fit statistics:

| Metric | Linear R2 | Linear relative RMSE | Quadratic R2 | Quadratic relative RMSE |
| --- | ---: | ---: | ---: | ---: |
| output_slew_ns | 0.948503 | 0.100339 | 0.949160 | 0.099696 |
| driven_cap_pf | 1.000000 | 4.38e-15 | 1.000000 | 2.34e-14 |
| delay_ns | 0.999756 | 0.007308 | 0.999853 | 0.005662 |
| power_w | 0.999994 | 0.001171 | 0.999999 | 0.000588 |
| source_boundary_net_switch_power_w | 1.000000 | 7.86e-15 | 1.000000 | 1.45e-14 |

Interpretation:

- Delay and power are very well approximated by low-order functions on iter-1.
- Driven capacitance and source-boundary switching power are effectively exact under this grouping.
- Output slew is usable but weaker than delay/power, with about 10% relative RMSE. This matters because output slew is a boundary state, so bucket conversion and feasibility margins need special care.

## Compose vs Direct Frontier Gap

Frontier counts:

```text
direct length_idx=1 frontier = 1095
direct length_idx=2 frontier = 4170
direct length_idx=3 frontier = 8610
iter-1 composed length_idx=2 frontier = 4095
iter-1 composed length_idx=3 frontier = 8310
```

Matched-key comparison:

| Target | Matched / Composed | Matched / Direct | Delay Ratio | Delay RMSE | Power Ratio | Power RMSE |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| length_idx=2, 50um | 63.74% | 62.59% | 1.0594 | 0.0131 ns | 1.0563 | 9.04e-7 W |
| length_idx=3, 75um | 34.30% | 33.10% | 1.0911 | 0.0198 ns | 1.1274 | 2.02e-6 W |

Bias:

```text
length_idx=2: composed_higher_delay_count = 2610 / 2610 matched
length_idx=2: composed_higher_power_count = 2610 / 2610 matched
length_idx=3: composed_higher_delay_count = 2850 / 2850 matched
length_idx=3: composed_higher_power_count = 2850 / 2850 matched
```

Interpretation:

- The composed result is systematically more conservative than direct characterization for every matched entry in this run.
- The average gap grows with composition depth: about +5.9% delay and +5.6% power at length_idx=2, then about +9.1% delay and +12.7% power at length_idx=3.
- Matched coverage drops from about 64% at length_idx=2 to about 34% at length_idx=3. This means the iter-1 basis does not reproduce a large portion of the direct longer-length frontier under exact key matching.

## Conclusion

The iter-1 characterization is a strong local fitting basis for delay and power. However, using only iter-1 frontier entries as a direct compositional replacement for iter-2 and iter-3 direct characterization is not yet sufficient by itself:

```text
fit quality: good, especially delay/power
compose fidelity: conservative but nontrivial gap
frontier coverage: degrades quickly by length_idx=3
```

The experiment supports using iter-1 characterization as a mathematical base model, but later analytical construction must account for accumulated boundary quantization, missing long-length frontier states, and systematic composition bias before replacing direct longer-iteration characterization.

## Follow-up: Function-Level Compose Result

The first compose experiment above used discrete characterization-entry composition. A follow-up experiment was added to evaluate function-level composition:

```bash
./bin/icts_test_module_characterization_realtech_regression --gtest_filter=CharacterizationRealTechExactRegressionTest.IterOneFunctionComposeGapReport
```

The report is written to:

```text
bin/icts_test_output/characterization/realtech/iter1_function_compose_gap/iter1_function_compose_gap_report.txt
```

Method:

1. Fit iter-1 response functions for each unit-length pattern.
2. Decompose each direct length-2 or length-3 pattern into unit slots.
3. Compose the fitted functions in continuous physical units by fixed-point propagation of internal slew and load-cap boundaries.
4. Compare the composed prediction with direct length-2 and length-3 characterization.

For frontier entries, function-level compose produced:

| Target | Basis | Evaluated / Direct | Delay Ratio | Delay RMSE | Power Ratio | Power RMSE |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| length_idx=2, 50um | linear | 4155 / 4170 | 1.0359 | 0.0101 ns | 1.0320 | 9.73e-7 W |
| length_idx=2, 50um | quadratic | 4155 / 4170 | 1.0384 | 0.0106 ns | 1.0312 | 9.61e-7 W |
| length_idx=3, 75um | linear | 8610 / 8610 | 1.0613 | 0.0171 ns | 1.0537 | 1.56e-6 W |
| length_idx=3, 75um | quadratic | 8610 / 8610 | 1.0632 | 0.0175 ns | 1.0530 | 1.55e-6 W |

Compared with discrete frontier compose:

| Target | Discrete Delay Ratio | Function Delay Ratio | Discrete Power Ratio | Function Power Ratio |
| --- | ---: | ---: | ---: | ---: |
| length_idx=2 | 1.0594 | 1.0359 to 1.0384 | 1.0563 | 1.0312 to 1.0320 |
| length_idx=3 | 1.0911 | 1.0613 to 1.0632 | 1.1274 | 1.0530 to 1.0537 |

Function-level composition therefore improves delay/power agreement and removes most of the discrete key-coverage issue. The caveat is that boundary response quality is weaker:

| Target | Basis | Output Slew Ratio | Output Slew RMSE | Driven Cap Ratio | Driven Cap RMSE |
| --- | --- | ---: | ---: | ---: | ---: |
| length_idx=2 | linear | 0.9374 | 0.0355 ns | 1.1890 | 0.00653 pF |
| length_idx=2 | quadratic | 0.9381 | 0.0356 ns | 1.1890 | 0.00653 pF |
| length_idx=3 | linear | 0.8690 | 0.0516 ns | 1.4528 | 0.0108 pF |
| length_idx=3 | quadratic | 0.8700 | 0.0517 ns | 1.4528 | 0.0108 pF |

Interpretation:

- Function-level compose is a better basis than discrete iter-1 frontier compose for delay and power.
- Linear and quadratic bases are nearly tied in this experiment; quadratic does not materially improve compose accuracy.
- Boundary metrics are the main risk. Output slew is systematically underestimated, while driven/source capacitance is systematically overestimated after recursive composition.
- A production analytical solver should treat boundary functions with conservative envelopes or calibration, not only fit delay/power accurately.
