# Integration Feasibility: Native H-Tree / Characterization Semantic Alignment

## User Hypothesis

The temporary-repo changes likely target the mismatch between:

- characterized/evaluated metric: h-tree root input -> leaf output delay
- actual evaluation STA after embedding/routing/root-driver sizing

This interpretation is plausible. The main mismatch sources in the current `ecc-tools-dev` code are:

1. `CharBuilder` still observes timing at the dummy sink input even for branch-buffered segment patterns.
2. `CharBuilder` passes raw `queryWireResistance()` into the temporary STA RC tree, while other STA RC paths convert milliohm to ohm.
3. Root-driver compensation adds delay/power but does not close the selected h-tree root boundary against physical root load and root-driver output slew.
4. `min_top_input_slew_ns` currently affects boundary filtering, but root-driver compensation uses a derived default input slew instead of the caller-provided root input slew.

## Clarification: Current Half-Input-Slew Usage

The current `half_input_slew` behavior is not part of `CharBuilder`'s characterization sampling.

Current code computes the root-driver compensation input slew as:

- `ResolveRootDriverCompensationInputSlewNs(char_builder.get_max_slew())`
- implementation: `max_slew_ns * 0.5`

That value is then stored in `RootDriverCompensationOptions.input_slew_ns` and used only by:

- `STAAdapter::queryRootDriverCostDirect(cell_master, input_slew_ns, load_cap_pf, clock_period_ns)`

By contrast, `CharBuilder` samples the full slew lattice:

- `_slews_to_test = get_slew_lattice().sampleValues()`
- for each `input_slew_ns` in `_slews_to_test`, it calls `setCharBufferInputSlew(input_slew_ns)` and stores the resulting `input_slew_idx`.

So in the current dev code:

- char-internal segment characterization covers the configured slew grid;
- the "half max slew" heuristic is only the Liberty lookup condition for root-driver compensation;
- it is not the h-tree root buffer's actual propagated output slew, because the current root-driver compensation does not query output slew.

Implication:

- Replacing the half-slew heuristic with `options.min_top_input_slew_ns` would affect root-driver compensation input condition only.
- Closing the h-tree root boundary still requires querying root-driver output slew and matching it to `HTreeTopologyChar::input_slew_idx`.

## Current Dev Baseline To Preserve

Current `ecc-tools-dev` contains fixes that the temporary repo does not:

- topology load distribution uses `max_leaf_load_count`;
- h-tree topology pattern composition tracks `source_exposed_load_count`;
- binary fanout legality is enforced during composition and root filtering;
- router overlapping terminals are legalized before FLUTE.

Integration must be additive. Do not overwrite `SegmentLibrary.hh`, `TopologyPruning.*`, `DepthPlan.*`, or routing code with the temporary repo versions because that would regress fanout legality and overlap robustness.

## Per-Change Integration Assessment

### 1. Char Timing Observation Pin

Feasibility: high.

Current code:

- `CharBuilderCircuit.cc` builds source, optional temp buffers, and dummy sink.
- `CharBuilderStaSampling.cc` calls `prepareCharTimingContext(_source_in_pin, _source_out_pin, _sink_in_pin)`.
- `STAAdapter::prepareCharTimingContext()` only requires the first two pins to be the source instance input/output; its third argument is just the observation vertex used by `queryCharClockAT()` and `queryCharSlew()`.

Integration shape:

- Add `_timing_observation_pin` to `CharBuilder`.
- In `createCharCircuit()`:
  - clear it at circuit start;
  - for the last inserted buffer, set it to that buffer's output pin;
  - if no inserted buffer exists, set it to `_sink_in_pin`.
- In `sampleFeasibleTopology()`, pass `_timing_observation_pin`.
- Clear it in `destroyCharCircuit()`.

Correctness impact:

- For branch-buffered segment patterns, output slew/delay becomes measured at the exposed buffer-output boundary, which matches h-tree composition and root-to-leaf-output evaluation semantics.
- For unbuffered segment patterns, behavior remains effectively unchanged.

Conflict risk:

- Low. Does not touch fanout state.
- Need a regression test or at least flow-level before/after reporting because delay numbers will change.

Recommendation:

- Integrate.

### 2. Char RC Resistance Unit Conversion

Feasibility: high.

Current code:

- `CharBuilderCircuit.cc` uses:
  - `queryWireResistance(_routing_layer, seg_len_um, _wire_width)`
  - passes result directly to `buildCharRcTree()`.
- Other code paths, such as full-design RC installation and wire RC diagnostics, divide `queryWireResistance()` by `1000.0` before using it as Ohm.

Integration shape:

- Add a local `constexpr double kMilliOhmPerOhm = 1000.0`.
- Use `queryWireResistance(...) / kMilliOhmPerOhm` in `setCharParasitics()`.

Correctness impact:

- Strong. If `queryWireResistance()` returns milliohms, current char STA sees 1000x resistance and therefore inflated RC delay/slew.
- This directly targets the root-input-to-leaf-output delay error.

Conflict risk:

- Very low.
- The only caveat is unit confirmation. Current internal diagnostics already imply milliohm-to-ohm conversion is expected.

Recommendation:

- Integrate early and validate with flow QoR/runtime; this is likely an unconditional bug fix.

### 3. Root-Driver Output Slew + Root Boundary Closure

Feasibility: medium-high, but should be integrated carefully.

Current code:

- `STAAdapter::RootDriverCost` has delay/power but no `output_slew_ns`.
- `RootDriverCompensationPass::apply()` evaluates root-driver delay/power and mutates each candidate's delay/power.
- It does not reject candidates whose raw h-tree root cap/slew buckets differ from the physical root-driver closure.
- Current fanout filtering happens after compensation and state-frontier pruning in `TopologyPruning.cc`; this must remain.

Integration shape:

- Add `output_slew_ns` to `STAAdapter::RootDriverCost`.
- Add direct Liberty output slew lookup using the same arc traversal style as delay lookup.
- Extend `RootDriverCompensationDetail` / report with `output_slew_ns` and `output_slew_bucket_idx`.
- Add `slew_lattice` to `RootDriverCompensationOptions`.
- Add a boundary check comparing:
  - `entry.get_driven_cap_idx()` vs physical root load bucket;
  - `entry.get_input_slew_idx()` vs root-driver output slew bucket.
- Change `RootDriverCompensationPass::apply()` to optionally filter to closed entries and return a result/stats object.
- In `TopologyPruning.cc`, keep current ordering:
  1. build composed h-tree frontier;
  2. apply root-driver compensation / closure;
  3. build state frontier;
  4. apply current root fanout legality filter.

Correctness impact:

- Strong. This makes the selected h-tree candidate's raw root boundary match the actual root driver and physical root closure.
- It targets exactly the char/evaluation semantic mismatch: candidate evaluation no longer combines a char entry from one root boundary with a physical root driver from another.

Conflict risk:

- Medium.
- Strict bucket equality can reject all candidates if the current char grid cannot produce the physical closure bucket pair.
- The temporary repo adds repair hints, but the native path does not implement a full native repair loop. The analytical repair loop is out of scope.
- If strict closure is enabled unconditionally, default flow may become more fragile.

Recommendation:

- Integrate in two stages:
  1. Add output slew query, bucket reporting, and mismatch diagnostics.
  2. Enable strict closure for candidate selection after validating candidate survival on `ics55_dev` with fanout 32 and fanout 4.
- If implementation should immediately fix the evaluation error, enable strict closure by default when `enable_root_driver_sizing=true`, but keep a diagnostic fallback path that reports `empty_frontier_after_root_boundary_closure` distinctly.

### 4. Root Driver Input Slew Should Respect `min_top_input_slew_ns`

Feasibility: high.

Current code:

- `ResolveBoundaryConstraints()` converts `options.min_top_input_slew_ns` into `top_input_slew_covering_idx`.
- `ResolveRootDriverCompensationInputSlewNs()` currently only derives a default from `char_builder.get_max_slew()`.
- The temporary repo changed this helper so caller-provided `min_top_input_slew_ns` becomes the root-driver Liberty input slew for compensation.

Why this matters:

- If the target metric is root input -> leaf output, root-driver compensation should use the modeled root input slew, not an unrelated default half of max slew.
- Otherwise, even a perfect root output boundary closure uses the wrong root-driver input condition.

Integration shape:

- Change root-driver input slew resolution to:
  - use `options.min_top_input_slew_ns` when present and positive;
  - otherwise use current fallback.
- Rename/report the meaning clearly as root-driver input slew to avoid confusion with h-tree top input slew.

Correctness impact:

- Strong when flow passes a meaningful root input slew.
- Low behavior risk when option is not set, because fallback remains unchanged.

Recommendation:

- Integrate with root boundary closure.

### 5. Top Input Slew Boundary Filter

Feasibility: do not integrate as-is.

Current code:

- `top_input_slew_covering_idx` is an active boundary constraint.
- It drives feasible/candidate split and fallback behavior.

Temporary repo behavior:

- Records `min_top_input_slew_ns` but stops computing `top_input_slew_covering_idx`, so `HasBoundaryConstraints()` becomes false for this condition.

Issue:

- This can be correct only if strict root-driver boundary closure fully replaces the old raw top-input-slew constraint.
- If root-driver sizing is disabled, strict closure is not active, so removing the old filter would silently stop enforcing `min_top_input_slew_ns`.

Recommendation:

- Do not copy this change directly.
- Keep current boundary constraint behavior unless strict root boundary closure is enabled.
- If strict closure is enabled, treat the old `top_input_slew_covering_idx` as redundant for final legality, but still use `min_top_input_slew_ns` to set root-driver input slew.
- If strict closure is disabled, retain the existing `top_input_slew_covering_idx` filter.

### 6. Reporting And Diagnostics

Feasibility: high.

Integration shape:

- Add root-driver output slew and bucket fields to `RootDriverCompensationReport`.
- Add root cap/slew bucket delta rows to `SolutionReport.cc`.
- Add per-level selected buffer count/area fields to `LevelPlan` and fill them during `ApplySelectedPatternToLevelPlans()`.
- Do not import analytical solver rows unless analytical support is intentionally enabled later.

Correctness impact:

- Does not directly change synthesis behavior.
- Makes the root boundary mismatch observable and makes evaluation-error debugging much easier.

Recommendation:

- Integrate with the functional root boundary work.

## Proposed Integration Order

1. Add char observation pin and RC unit conversion.
2. Add root-driver output slew query and reporting.
3. Add root boundary closure diagnostics without filtering, so the flow can show bucket deltas.
4. Add strict root boundary filtering while preserving current fanout filtering.
5. Revisit `top_input_slew_covering_idx` behavior only after strict closure is validated.

## Validation Plan

Minimum targeted checks:

- Build:
  - `ninja -C build iEDA`
- Unit/regression:
  - existing routing overlap legalization tests should still pass.
  - add or run a characterization-focused test if available; otherwise inspect flow report bucket deltas.
- Flow acceptance:
  - `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- Compare:
  - H-tree selected delay/power;
  - evaluation STA root input -> leaf output error if available;
  - `root_cap_bucket_delta`;
  - `root_slew_bucket_delta`;
  - fanout violations in setup/hold skew reports.

Regression constraints:

- fanout=4 must remain legal.
- fanout=32 baseline should not fail.
- no wholesale import from `/home/liweiguo/project/ecc-tools`.

## Final Feasibility Answer

Yes, the useful changes can be integrated into current `ecc-tools-dev`, but not by direct merge.

Recommended integration:

- integrate char observation pin;
- integrate RC unit conversion;
- integrate root-driver output slew and boundary diagnostics;
- integrate strict root-boundary closure with current fanout legality preserved;
- do not directly disable `top_input_slew_covering_idx`; reinterpret it only under strict root-driver closure.

This set directly addresses root-input-to-leaf-output delay mismatch while keeping the previous small-fanout fixes intact.
