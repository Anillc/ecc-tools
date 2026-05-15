# Research: structural-cap function-level compose experiment

- Scope: P1 validation for using iter-1 fitted functions plus structural capacitance operators
- Date: 2026-05-14
- Test command:

```bash
cmake --build build --target icts_test_module_characterization_realtech_regression -j 8
./bin/icts_test_module_characterization_realtech_regression --gtest_filter=CharacterizationRealTechExactRegressionTest.IterOneStructuralCapFunctionComposeGapReport
```

The report is written to:

```text
bin/icts_test_output/characterization/realtech/iter1_structural_cap_function_compose_gap/iter1_structural_cap_function_compose_gap_report.txt
```

## Method

The experiment reuses the previous function-level compose setup, but replaces fitted `driven_cap` propagation with physical structural capacitance operators:

```text
wire-only unit:
  C_source(c) = c + C_wire

buffered unit:
  C_source(c) = C_in(first_buffer) + C_prewire
```

`F/D/P/W` are still fitted from iter-1 samples. Capacitance is propagated in continuous physical units and compared to native characterization only at the final source boundary through native-compatible covering bucket semantics.

The test reports both:

- physical structural-cap prediction gap, before bucket conversion
- bucketed structural-cap prediction gap, after native-compatible covering conversion

## Unit-Level Cap Result

Iter-1 structural cap operators:

```text
total operators = 5
wire operators = 1
buffered operators = 4
missing operator count = 0
```

Physical structural cap values are lower than direct stored bucket representatives:

| Scope | Driven Cap Ratio | RMSE |
| --- | ---: | ---: |
| iter-1 physical structural cap | 0.7704 | 0.00730 pF |

After applying the native-compatible covering bucket at the comparison boundary, iter-1 source cap matches exactly:

| Scope | Driven Cap Ratio | RMSE |
| --- | ---: | ---: |
| iter-1 bucketed structural cap | 1.0000 | 0.00000 pF |

Interpretation:

- Native `SegmentChar` stores bucket indices, not raw physical cap values.
- Internal analytical propagation should use physical cap.
- Native compatibility checks should bucket only at validation/reporting boundaries.

## Frontier Compose Gap

For frontier entries, structural cap gives exact driven-cap bucket agreement at length 2 and length 3:

| Target | Basis | Evaluated / Direct | Driven Cap Ratio | Driven Cap RMSE |
| --- | --- | ---: | ---: | ---: |
| length_idx=2 | linear | 4155 / 4170 | 1.0000 | 0.00000 pF |
| length_idx=2 | quadratic | 4155 / 4170 | 1.0000 | 0.00000 pF |
| length_idx=3 | linear | 8595 / 8610 | 1.0000 | 0.00000 pF |
| length_idx=3 | quadratic | 8595 / 8610 | 1.0000 | 0.00000 pF |

Delay and power agreement also improves materially compared with the earlier fitted-cap function compose:

| Target | Basis | Delay Ratio | Delay RMSE | Power Ratio | Power RMSE |
| --- | --- | ---: | ---: | ---: | ---: |
| length_idx=2 | linear | 1.0022 | 0.00391 ns | 0.9874 | 5.07e-7 W |
| length_idx=2 | quadratic | 1.0055 | 0.00446 ns | 0.9864 | 5.04e-7 W |
| length_idx=3 | linear | 0.9969 | 0.00411 ns | 0.9688 | 7.91e-7 W |
| length_idx=3 | quadratic | 1.0001 | 0.00437 ns | 0.9677 | 7.97e-7 W |

Previous fitted-cap function compose frontier result for reference:

| Target | Basis | Delay Ratio | Power Ratio | Driven Cap Ratio |
| --- | --- | ---: | ---: | ---: |
| length_idx=2 | linear | 1.0359 | 1.0320 | 1.1890 |
| length_idx=3 | linear | 1.0613 | 1.0537 | 1.4528 |

## Remaining Boundary Risk

Output slew does not improve automatically:

| Target | Basis | Output Slew Ratio | Output Slew RMSE |
| --- | --- | ---: | ---: |
| length_idx=2 | linear | 0.9372 | 0.0356 ns |
| length_idx=2 | quadratic | 0.9382 | 0.0357 ns |
| length_idx=3 | linear | 0.8687 | 0.0517 ns |
| length_idx=3 | quadratic | 0.8700 | 0.0518 ns |

This confirms that the analytical route should keep capacitance as an exact structural state, but must use a conservative output-slew envelope for feasibility.

## Conclusion

P1 supports the structural-cap technical base:

```text
driven_cap: structural physical propagation + final native-compatible bucket conversion
F/D/P/W: iter-1 fitted functions
```

The route is suitable for P2-P7 planning. The next implementation gate is not capacitance fidelity; it is output-slew safety and power ownership calibration.
