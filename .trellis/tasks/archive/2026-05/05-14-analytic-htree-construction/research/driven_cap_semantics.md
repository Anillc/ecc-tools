# Research: driven cap semantics and native assignment source

- Scope: code-path lookup
- Date: 2026-05-14

## Summary

`driven_cap_idx` is the source-side boundary capacitance bucket of a segment or topology candidate. It is the capacitance seen by the upstream driver when driving the candidate. It is not sampled from STA as a slew-dependent response.

Native characterization assigns physical `driven_cap_pf` before sweeping input slews:

```text
if the pattern has at least one buffer:
  driven_cap_pf = input_pin_cap(first_buffer)
                  + wire_cap(source_to_first_buffer)

else:
  driven_cap_pf = load_pf
                  + sum(wire_cap(all_wire_segments))
```

Therefore:

- For buffered patterns, `driven_cap_pf` depends on the first buffer and source-to-first-buffer wire segment, and is independent of `input_slew` and downstream `load_pf`.
- For wire-only patterns, `driven_cap_pf` is an affine expression in `load_pf` with slope 1 plus the total wire capacitance.
- For patterns whose first buffer appears after one or more unit slots, recursive composition can compute the same source boundary capacitance by propagating downstream source boundary cap through wire-only prefix slots.

## Native Assignment Path

The assignment is in `CharBuilder::sampleFeasibleTopology`:

- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc:80`
- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc:82`
- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc:83`
- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc:85`
- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc:86`
- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc:87`

The computed physical value is checked against the cap lattice and passed into slew sampling:

- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc:91`
- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc:94`
- `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc:103`

Inside `sampleLoadSlews`, the value is reused for every input slew sample:

- `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc:45`
- `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc:60`
- `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc:99`
- `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc:106`

`tryMakeStoredSampleIndices` only converts this physical value to a cap bucket:

- `src/operation/iCTS/source/module/characterization/CharBuilderSampleStorage.cc:32`
- `src/operation/iCTS/source/module/characterization/CharBuilderSampleStorage.cc:38`
- `src/operation/iCTS/source/module/characterization/CharBuilderSampleStorage.cc:50`
- `src/operation/iCTS/source/module/characterization/CharBuilderSampleStorage.cc:56`

## Native Usage

Serial segment composition uses `driven_cap_idx` as the source boundary cap of the composed result:

- `src/operation/iCTS/source/database/characterization/SegmentChar.hh:67`
- `src/operation/iCTS/source/database/characterization/SegmentChar.hh:81`

The segment hash-join requires:

```text
upstream.output_slew_idx == downstream.input_slew_idx
upstream.load_cap_idx == downstream.driven_cap_idx
```

Source:

- `src/operation/iCTS/source/module/characterization/SegmentTraits.hh:34`
- `src/operation/iCTS/source/module/characterization/SegmentTraits.hh:45`
- `src/operation/iCTS/source/module/characterization/SegmentTraits.hh:52`

H-tree topology composition also carries the upstream driven cap as the composed source boundary cap:

- `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh:81`
- `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh:99`

The H-tree join applies binary fanout by matching:

```text
ceil(upstream.load_cap_idx / 2) == downstream.driven_cap_idx
```

Source:

- `src/operation/iCTS/source/module/characterization/HTreeTraits.hh:35`
- `src/operation/iCTS/source/module/characterization/HTreeTraits.hh:43`
- `src/operation/iCTS/source/module/characterization/HTreeTraits.hh:57`

Root-driver compensation uses `driven_cap_idx` as the raw source boundary cap bucket and checks it against the physical source boundary bucket:

- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc:641`
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc:646`

## Analytical Solver Implication

`driven_cap` should not be treated as a general fitted response surface like delay or output slew.

A better analytical model is structural:

```text
buffered unit:
  driven_cap = C_in(first_buffer) + C_wire(source_to_first_buffer)

wire-only unit:
  driven_cap = downstream_load + C_wire(unit_wire)
```

For a composed segment, compute source boundary capacitance recursively from the downstream side:

```text
c_source(last_unit) = local_driven_cap(last_unit, final_load)
c_source(i)         = local_driven_cap(unit_i, c_source(i+1))
```

Then only convert the final source boundary capacitance to a lattice bucket when interacting with native validation or reporting.

The previous function-level compose experiment fitted `driven_cap` as a response surface only as an exploratory measurement. The observed systematic overestimation is a signal that the production analytical path should switch `driven_cap` to a structural formula with bucket-aware rounding, not tune a more complex fit.
