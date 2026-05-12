# Native H-Tree / Characterization Diff Review

## Scope

Compared:

- Current repo: `/home/liweiguo/project/ecc-tools-dev`
- Temporary repo: `/home/liweiguo/project/ecc-tools`

Included:

- `src/operation/iCTS/source/module/characterization`
- `src/operation/iCTS/source/flow/synthesis/htree`
- supporting `STAAdapter` / config changes only where they affect native h-tree characterization or root boundary semantics

Excluded:

- `.trellis`
- `source/flow/synthesis/htree/analytical_solver`
- `source/module/analytical_characterization`
- analytical solver CMake/test additions
- analytical-only reporting rows unless they expose a native semantic issue

Important merge note:

- `/home/liweiguo/project/ecc-tools` appears to be based before the current `ecc-tools-dev` fanout-legality work. It removes or lacks `source_exposed_load_count`, `HTreeFanoutPruningOptions`, topology `max_leaf_load_count`, and the router overlap legalization test/fix that exist in `ecc-tools-dev`.
- Those differences are not optimizations from the temporary repo. If any temporary-repo ideas are ported back, preserve the current fanout legality and overlap-routing fixes from `ecc-tools-dev`.

## Executive Summary

The temporary repo's native h-tree / char changes target one main problem: the old characterization data and h-tree build process mixed abstract characterization boundary values with physical root-driver and terminal-buffer behavior. The useful native changes are:

1. Characterization now observes timing at the semantic output boundary of a buffered segment, not always at the dummy sink pin.
2. Characterization RC now converts queried wire resistance from milliohms to ohms before building STA RC trees.
3. Root-driver compensation now queries output slew, maps it to the char slew lattice, and can strictly close the selected h-tree root boundary by matching both cap and slew buckets.
4. The old top-input-slew boundary filter is effectively disabled for native search; root boundary matching is moved to root-driver closure.
5. Reporting now exposes root cap/slew bucket deltas and per-level buffer pressure, making semantic drift visible.

The strongest correctness improvement is #3. The highest-risk change is #4, because if root-driver sizing/strict closure is disabled, the previous raw `top_input_slew_covering_idx` constraint is no longer enforced.

## Change Points

### 1. Characterization Timing Observation Pin Follows Segment Boundary Semantics

Files:

- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/module/characterization/CharBuilder.hh`
- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/module/characterization/CharBuilderCircuit.cc`
- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc`

What changed:

- Added `_timing_observation_pin`.
- During char circuit construction, if a topology has inserted buffers, `_timing_observation_pin` is set to the output pin of the last inserted buffer.
- If the topology has no inserted buffers, `_timing_observation_pin` falls back to the dummy sink input pin.
- STA timing context uses `_timing_observation_pin` instead of always using `_sink_in_pin`.

Key references:

- `CharBuilderCircuit.cc:57` clears the observation pin for the new circuit.
- `CharBuilderCircuit.cc:87` to `CharBuilderCircuit.cc:95` selects last-buffer output or sink input.
- `CharBuilderStaSampling.cc:55` passes the selected observation pin to `prepareCharTimingContext`.

Motivation:

- Native h-tree segment composition distinguishes `kLeafUnbuffered` from `kBranchBuffered`.
- For a branch-buffered segment, the meaningful boundary for the next h-tree level is the last real buffer output, not the dummy sink input behind the output net.
- Always observing the sink input makes the stored output slew/delay include downstream passive net/sink effects even when the pattern semantically exposes a buffered boundary.

Correctness:

- Correct for branch-buffered patterns because the observed pin is the actual driver boundary consumed by the next composition step.
- Correct for unbuffered patterns because there is no real buffer boundary, so the sink input remains the only timing endpoint.
- The output net parasitic/load is still present, so the last buffer's output slew is computed under the downstream load; the change only moves the observed endpoint to the semantic boundary.
- Risk: if any downstream code still interprets segment delay as source-to-dummy-sink arrival for branch-buffered patterns, this changes that metric. From the h-tree composition semantics, the boundary-based interpretation is the coherent one.

### 2. Characterization RC Resistance Unit Fix

File:

- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/module/characterization/CharBuilderCircuit.cc`

What changed:

- `setCharParasitics()` divides `STA_ADAPTER_INST.queryWireResistance(...)` by `1000.0` before passing it to `buildCharRcTree()`.

Key references:

- `CharBuilderCircuit.cc:39` defines `kMilliOhmPerOhm`.
- `CharBuilderCircuit.cc:110` applies the conversion.

Motivation:

- Other STA RC installation paths already treat `queryWireResistance()` as milliohms and divide by `1000.0` before giving resistance to STA.
- Characterization previously passed the raw value into `makeResistor()`, making temporary char RC resistance 1000x too large if the adapter returns milliohms.
- That would inflate characterized delay/slew and bias h-tree pattern selection.

Correctness:

- This is consistent with existing full-design RC tree code and wire RC diagnostics, which convert the query result to Ohm.
- `buildCharRcTree()` ultimately creates STA resistor elements, so Ohm is the expected unit at that boundary.
- This should reduce artificial RC pessimism in char samples and make native h-tree char closer to post-build STA behavior.

### 3. Root-Driver Compensation Now Closes the H-Tree Root Boundary

Files:

- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh`
- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/database/adapter/sta/STAAdapterRootDriverQuery.cc`
- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.hh`
- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`
- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`

What changed:

- `STAAdapter::RootDriverCost` now includes `output_slew_ns`.
- Direct root-driver Liberty lookup now queries worst output slew in addition to delay and power.
- `RootDriverCompensationOptions` now carries a slew lattice and strict boundary closure flags.
- `RootDriverBoundaryClosureCheck` compares:
  - raw char top/source cap bucket: `entry.get_driven_cap_idx()`
  - physical root closure load bucket: `compensation.load_bucket_idx`
  - raw char top input slew bucket: `entry.get_input_slew_idx()`
  - physical root-driver output slew bucket: `compensation.output_slew_bucket_idx`
- With strict closure enabled, candidates whose cap/slew buckets do not match are rejected before frontier pruning.
- `HTree::build()` enables strict closure when root-driver sizing is enabled.

Key references:

- `STAAdapterRootDriverQuery.cc:97` to `STAAdapterRootDriverQuery.cc:122` computes root-driver output slew.
- `STAAdapterRootDriverQuery.cc:240` to `STAAdapterRootDriverQuery.cc:245` stores output slew and uses it in validity.
- `RootDriverCompensation.hh:68` to `RootDriverCompensation.hh:78` adds the slew lattice and strict closure knobs.
- `RootDriverCompensation.hh:105` to `RootDriverCompensation.hh:129` defines the boundary closure check/result.
- `RootDriverCompensation.cc:459` to `RootDriverCompensation.cc:484` maps output slew into the slew lattice.
- `RootDriverCompensation.cc:588` to `RootDriverCompensation.cc:604` compares raw char buckets with physical closure buckets.
- `RootDriverCompensation.cc:698` to `RootDriverCompensation.cc:743` rejects non-closed entries under strict closure.
- `HTree.cc:521` to `HTree.cc:529` passes cap/slew lattices and enables strict closure.
- `TopologyPruning.cc:270` to `TopologyPruning.cc:285` applies compensation before final state frontier pruning.

Motivation:

- Previous root-driver compensation added root cell delay/power to h-tree chars, but the selected char entry could still have a source cap/slew bucket that did not match the physical root driver and root closure load.
- That creates a semantic drift: a candidate may be selected under one abstract root boundary, then evaluated/embedded under a different physical root boundary.
- Strict closure turns this into an explicit legality condition instead of a silent modeling error.

Correctness:

- Matching buckets is the right invariant because native characterization stores cap/slew as lattice indices, and h-tree composition operates on those indices.
- Physical root closure load is resolved from actual topology and selected patterns, not guessed from the abstract char entry.
- Root output slew comes directly from Liberty under the physical load, so the selected h-tree top input slew matches the driver that will feed it.
- Applying compensation before final frontier pruning is correct because delay/power ordering should include the root-driver cost for any root-boundary-closed candidate.
- Risk: strict closure can reject all candidates if the char lattice cannot represent the physical load/slew bucket or if no candidate with matching buckets exists. The temporary repo records repair hints, but the native path does not appear to perform a repair loop; the repair-loop usage is in analytical code, which is out of scope here.

### 4. Top Input Slew Boundary Filter Is Reinterpreted / Mostly Disabled

File:

- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/flow/synthesis/htree/constraint/Constraint.cc`

What changed:

- `ResolveBoundaryConstraints()` keeps `min_top_input_slew_ns` but no longer computes `top_input_slew_covering_idx`.
- `HasBoundaryConstraints()` still depends on `top_input_slew_covering_idx`, so this disables the old raw h-tree top-input-slew feasible/candidate split for normal native search.

Key references:

- `Constraint.cc:45` to `Constraint.cc:55` records the min slew but does not map it to the char slew lattice.
- `Constraint.cc:58` to `Constraint.cc:61` still treats only `top_input_slew_covering_idx` as an active boundary constraint.

Motivation:

- With root-driver closure, the raw h-tree top input slew should be the actual output slew of the root driver, not an independent pre-filter.
- The temporary repo moves top boundary consistency from "pick a char entry with at least this input slew index" to "pick a char entry whose input slew bucket equals the physical root-driver output slew bucket."

Correctness:

- Correct when root-driver sizing/strict closure is enabled, because the physical root output slew closes the boundary.
- The approach avoids selecting candidates that satisfy an abstract min slew but mismatch the actual root driver.
- Risk: when root-driver sizing is disabled, strict closure is not enabled in `HTree.cc`, and the old `top_input_slew_covering_idx` filter is also disabled. In that mode, a caller-provided `min_top_input_slew_ns` no longer constrains raw h-tree top input slew. That should be treated as either intentional policy or a bug, depending on how immutable top-level clock sources are supposed to be modeled.

### 5. Native H-Tree Selection Report Now Exposes Semantic Drift Diagnostics

Files:

- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/flow/synthesis/htree/HTree.hh`
- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `/home/liweiguo/project/ecc-tools/src/operation/iCTS/source/flow/synthesis/htree/solution/SolutionReport.cc`

What changed:

- `LevelPlan` records selected per-level buffer count and area, plus multiplicity-weighted count and area.
- Selected pattern application fills these fields from the selected segment pattern.
- Synthesis report now emits:
  - per-level buffer counts and weighted counts;
  - per-level buffer area and weighted area;
  - root-driver input and output slew;
  - raw char source cap bucket vs physical root load bucket;
  - raw char top input slew bucket vs root output slew bucket;
  - bucket deltas.

Key references:

- `HTree.hh` adds `selected_buffer_count`, `selected_buffer_area_um2`, `selected_weighted_buffer_count`, and `selected_weighted_buffer_area_um2`.
- `HTree.cc:370` to `HTree.cc:386` computes these values.
- `SolutionReport.cc:197` to `SolutionReport.cc:202` reports buffer pressure.
- `SolutionReport.cc:246` to `SolutionReport.cc:260` reports root cap/slew closure diagnostics.

Motivation:

- The user-visible logs now show whether the selected raw char entry and physical root closure agree.
- Weighted buffer count/area matters because one pattern choice at level `L` expands over `2^L` topology branches.
- This makes QoR changes and root-boundary semantic mismatches explainable without opening debugger state.

Correctness:

- This is mostly diagnostic and does not directly change synthesis behavior.
- Weighted multiplicity matches binary h-tree level expansion.
- Cell area comes from Liberty through STA adapter, which is the appropriate source for comparing buffer area pressure.

## Differences Not Counted As Temporary-Repo Native Optimizations

### Analytical H-Tree / Analytical Characterization

The temporary repo adds:

- `source/flow/synthesis/htree/analytical_solver`
- `source/module/analytical_characterization`
- `enable_analytical_htree` config wiring
- fitted/analytical mode reporting and forced iteration-one characterization

These are explicitly out of scope for this review. The native code paths are affected only where shared hooks were added, such as root-driver boundary closure and reporting.

### Current Dev Fanout / Router Fixes Missing From Temporary Repo

The temporary repo lacks current `ecc-tools-dev` fanout and routing robustness changes:

- `PatternCompositionState::source_exposed_load_count`
- `HTreeFanoutPruningOptions`
- topology `max_leaf_load_count`
- fanout-aware topology composition and root filtering
- overlapping-terminal legalization before FLUTE in router tests/fix

This matters because merging the temporary repo wholesale would regress the previously fixed small-fanout behavior. These removals should not be interpreted as performance/semantic optimizations.

## Correctness Assessment By Import Priority

1. Import candidate: char timing observation pin.
   - Strong motivation and clean semantic match with branch-buffered vs unbuffered segment boundaries.
   - Should be paired with focused regression tests for buffered and unbuffered char topologies.

2. Import candidate: char RC resistance conversion.
   - High-confidence unit fix.
   - Should be verified by comparing a known wire segment RC in char context and full-design RC context.

3. Import candidate: root-driver output slew and strict boundary closure.
   - High-value semantic fix.
   - Needs careful integration with current fanout legality state and with native fallback behavior when strict closure rejects all candidates.

4. Import candidate with caution: disabling raw top-input-slew boundary index.
   - Correct if root-driver closure is the sole source of top boundary truth.
   - Risky if there are flows with root-driver sizing disabled but still relying on `min_top_input_slew_ns`.

5. Import candidate: diagnostic report fields.
   - Useful and low-risk, but analytical-only rows should be avoided or hidden when not building analytical support.

## Recommended Validation If Ported

- Unit/regression for buffered char topology:
  - one topology with at least one inserted buffer should observe output slew at last buffer output;
  - one unbuffered topology should still observe sink input.
- RC unit smoke test:
  - compare char net resistance with full-design RC installation for the same layer/length.
- Native h-tree flow:
  - run the existing `ics55_dev` flow with default fanout and fanout=4.
  - confirm `root_cap_bucket_delta == 0` and `root_slew_bucket_delta == 0` for strict-closed selected solutions.
- Fanout regression:
  - preserve current `ecc-tools-dev` max-fanout checks and confirm STA skew fanout reports remain within constraint.
