# Root Driver Delay/Power Compensation Research

## Scope

This research covers the H-tree root driver layer only:

* root driver input-to-output cell arc delay;
* root driver cell power, primarily Liberty internal power and leakage;
* root output net switching power semantics.

The root buffering policy is not changed. The compensated delay/power is a design-level abstract metric used by H-tree candidate selection; it is not a post-embedding STA/iPW signoff metric.

## Existing Semantics

The current H-tree final `best_char.delay/power` is selected from abstract characterization entries, not from the embedded CTS object graph.

Relevant code:

* `src/operation/iCTS/source/module/characterization/CharBuilderCircuit.cc`
  * Characterization builds a temporary source buffer, candidate segment objects, and a temporary sink buffer.
  * The clock is created on the virtual source buffer output pin.
* `src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc`
  * `power_inst_names` is initialized from `_temp_inst_names`, so the virtual source and sink buffers are excluded from sampled cell power.
  * `_temp_net_names` are included, so segment net switching power is included.
* `src/operation/iCTS/source/module/characterization/CharBuilderSlewSampling.cc`
  * `delay_ns = queryCharClockAT(...)` measures from virtual source buffer output to virtual sink input.
  * `source_boundary_net_switch_power` is saved from `_temp_net_names.front()`.
* `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh`
  * H-tree composition adds delay.
  * Power is composed as `up.power + 2 * (down.power - down.source_boundary_net_switch_power)`.
  * The top/root source-boundary net switching term is preserved exactly once.
* `src/operation/iCTS/source/flow/synthesis/htree/embedding/Embedding.cc`
  * After selection, the physical `root_net` is reconnected from `root_output_pin` to the selected H-tree entry loads.
  * Root driver sizing is applied after embedding selection via `ApplyRootDriverSizing`.

Therefore, for sink-side H-trees, current selected metrics start at the root driver output boundary. The root driver input-to-output cell arc delay and root driver cell power are missing. The root output net switching term is already represented by the top source-boundary net switching term in the selected H-tree power.

## Can Delay Be Computed Without Full iSTA/RC Tree?

Yes.

iSTA Liberty already exposes the needed table lookup APIs:

* `LibArcSet::getDelayOrConstrainCheckNs(input_trans_type, output_trans_type, slew, load)`
* `LibArc::getDelayOrConstrainCheckNs(trans_type, slew, load)`
* `STAAdapterInternal::FindBufferArcSet(lib_cell)`
* `STAAdapterInternal::ConvertPfLoadToLibUnit(lib_cell, load_pf)`

Existing users:

* `src/operation/iSTA/source/module/sta/StaDelayPropagation.cc`
* `src/operation/iSTA/source/module/sta/StaDataSlewDelayPropagation.cc`
* `src/operation/iSTA/source/module/sdc-cmd/CmdSetDrivingCell.cc`

Minimal input for root cell delay:

* root driver cell master;
* root driver input slew in ns;
* root driver output load cap in pF;
* buffer/inverter arc sense and Liberty unit conversion.

For candidate compensation, the abstract values should come from the selected entry bins:

* `input_slew_ns = slew_lattice.valueForIndex(entry.get_input_slew_idx())`;
* `output_load_pf = cap_lattice.valueForIndex(entry.get_driven_cap_idx())`.

The lookup should evaluate both rise and fall paths and use the max value, consistent with the current scalar max-delay characterization semantics.

## Can Cell Power Be Computed Without Full iPW Graph?

Mostly yes for the root buffer case.

The Liberty parser exposes direct internal-power tables:

* `LibCell::findLibertyPowerArcSet(from_port, to_port)`
* `LibPowerArcSet::get_power_arcs()`
* `LibPowerArc::get_internal_power_info()`
* `LibInternalPowerInfo::gatePower(trans_type, slew, load)`
* `LibCell::convertInternalPowerTableToMwNs(...)`

iPW's own internal-power flow uses the same Liberty information:

* `src/operation/iPA/source/module/ops/calc_power/PwrCalcInternalPower.cc`
  * output-pin internal power looks up rise/fall energy from Liberty using input slew and output load;
  * the rise/fall energy is averaged;
  * averaged energy is multiplied by output toggle to get mW, then converted to W.

For root driver compensation, use the same reference toggle basis as CharBuilder power sampling. The current char-only power context creates a 10 ns clock and annotates `c_default_clock_toggle / clock_period`, so the equivalent toggle is:

```text
root_toggle_per_ns = c_default_clock_toggle / 10.0
```

Direct internal-power estimate:

```text
rise_energy_mw_ns = liberty_rise_power(input_slew_ns, output_load_lib_unit)
fall_energy_mw_ns = liberty_fall_power(input_slew_ns, output_load_lib_unit)
avg_energy_mw_ns  = (rise_energy_mw_ns + fall_energy_mw_ns) / 2
internal_power_w  = root_toggle_per_ns * avg_energy_mw_ns / ipower::g_mw2w
```

Leakage can also be queried without iPW:

* `LibCell::get_cell_leakage_power()`
* `LibCell::getLeakagePowerList()`

However, exact conditional leakage parity with iPW would require signal probability for Liberty `when` conditions. For this root-buffer task, a pragmatic implementation can:

* include unconditional/default cell leakage when available;
* avoid trying to evaluate complex `when` expressions in the H-tree candidate loop;
* document the leakage term as an abstract Liberty leakage estimate.

## Can Root Net Power Be Computed Without Full iPW/RC Tree?

Exact physical root net power: no.

The exact post-embedding root net switching power depends on the final embedded net's load cap and parasitics. Without constructing or updating the RC tree, there is no strict source for actual physical net capacitance.

Abstract candidate-level root output net power: yes, but it is already present.

Current iPW switching formula is:

```text
switch_power_w = c_switch_power_K * toggle * net_cap_pf * vdd^2 / ipower::g_mw2w
```

Relevant code:

* `src/operation/iCTS/source/database/adapter/sta/STAAdapterCharPower.cc`
* `src/operation/iPA/source/module/ops/calc_power/PwrCalcSwitchPower.cc`

For H-tree characterization, the first temporary net switching power is stored in `source_boundary_net_switch_power`, and H-tree composition preserves the top/root source-boundary net once. That term semantically corresponds to the root driver's output net in the selected H-tree abstraction.

Implementation consequence:

* do not add root output net switching power again as compensation;
* optionally expose/report `best_char.get_source_boundary_net_switch_power()` as the selected abstract root output net switching power;
* only add a root net power delta if a future implementation intentionally replaces the existing source-boundary net model with a new model.

Root input net power is upstream of the H-tree root driver and should remain owned by source-trunk/upstream clock-net semantics, not by this H-tree root-driver compensation.

## Recommended Injection Point

The best injection point is inside `EvaluateCandidateBuild`, after candidate entries are fully composed and sink-load-region filtered, but before per-depth best selection and before the global pools receive references.

Current flow:

* `BuildPatternSearch(...)` creates composed raw topology entries.
* `FilterSinkLoadRegionLegalEntries(...)` creates `candidate_frontier_entries` and/or `feasible_frontier_entries`.
* `SelectBestHTreeChar(...)` selects the per-depth best entry.
* `DepthPlan.cc::RecordTopologyDepthCandidateResult(...)` records selected delay/power.
* `DepthPlan.cc::AppendGlobalCandidateRefs(...)` pushes refs into global feasible/candidate pools.
* `HTree.cc::SelectBestGlobalEntry(...)` selects across depths.

Compensation should happen after `FilterSinkLoadRegionLegalEntries(...)` populates:

* `result.candidate_frontier_entries`;
* `result.feasible_frontier_entries`.

and before:

* `SelectBestHTreeChar(...)`;
* `RecordTopologyDepthCandidateResult(...)`;
* `AppendGlobalCandidateRefs(...)`.

This matches the desired semantics:

* the hash-join composition remains based on raw electrical boundary states;
* root compensation is not repeatedly applied during intermediate composition;
* per-depth and global Pareto selection see compensated design-level delay/power;
* global pool entries point at already-compensated frontier entries.

## Candidate Root Driver Cell Resolution

The selected root driver cell can vary by candidate because root driver sizing currently derives from selected level segment metadata:

* `HTree.cc::ResolveSelectedRootDriverCellMaster(...)`
* final selected level data uses each level segment pattern's `cell_masters.back()`.

For candidate-level compensation, resolve the candidate root driver master from the candidate topology pattern:

1. Materialize the topology pattern with `TopologyPatternLibrary::materialize(entry.get_pattern_id())`.
2. Iterate level segment pattern ids from root to leaf.
3. For the first segment pattern with non-empty `cell_masters`, use `cell_masters.back()`, matching current final selected root sizing logic.
4. If no candidate master is found, fall back to the current root driver instance master when the root driver is a buffer and the compensation context is enabled.

This should be cached by `PatternId` and by `(cell_master, input_slew_idx, driven_cap_idx)` to avoid redundant Liberty lookups.

## Residual Semantic Gap

This task compensates the missing root cell arc cost. It does not re-characterize downstream waveform propagation with the actual root driver cell.

Current segment characterization applies input slew to a virtual source buffer and propagates from that virtual source output. If the actual root driver master differs from the virtual source buffer used during characterization, downstream delay/slew still reflect the virtual source output model. Fixing that would require characterizing source-driver alternatives or adding driver-aware waveform modeling, which is out of scope for this task.

## Proposed Implementation Files

Likely affected files:

* `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh`
* `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.hh`
* `src/operation/iCTS/source/database/adapter/sta/STAAdapterCellQuery.cc` or a new `STAAdapterRootDriverQuery.cc`
* `src/operation/iCTS/source/database/adapter/sta/CMakeLists.txt`
* `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.hh`
* `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
* optionally a new `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/RootDriverCompensation.{hh,cc}`
* `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/CMakeLists.txt`
* `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.hh`
* `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.cc`
* `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
* `src/operation/iCTS/source/flow/synthesis/htree/HTree.hh`
* `src/operation/iCTS/source/flow/synthesis/htree/solution/SolutionReport.cc`

Suggested public adapter API:

```c++
struct BufferRootDriverCost {
  bool valid = false;
  double cell_delay_ns = 0.0;
  double internal_power_w = 0.0;
  double leakage_power_w = 0.0;
};

static auto queryBufferRootDriverCost(
    const std::string& cell_master,
    double input_slew_ns,
    double output_load_pf,
    double reference_clock_period_ns) -> BufferRootDriverCost;
```

Suggested compensation rule:

```text
compensated_delay = raw_delay + root_cell_delay
compensated_power = raw_power + root_cell_internal_power + root_cell_leakage_power
root_output_net_switch_power_delta = 0
```

The selected abstract root output net switching power can be reported from:

```text
best_char.get_source_boundary_net_switch_power()
```

