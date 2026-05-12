# Design: Native H-Tree / Characterization Semantic Alignment

## Technical Design

This task integrates only native h-tree and characterization semantic fixes from the temporary `/home/liweiguo/project/ecc-tools` branch. The implementation must stay additive to the current `ecc-tools-dev` codebase and preserve the existing small-fanout legality and router legalization fixes.

### Characterization Boundary Semantics

`CharBuilder` will distinguish the physical dummy sink from the timing observation boundary:

- For unbuffered segment patterns, observe timing at the dummy sink input, preserving the existing semantics.
- For buffered segment patterns, observe timing at the last inserted buffer output, which is the boundary consumed by native h-tree composition.

The char circuit still contains the downstream dummy sink and parasitics, so the last buffer output is characterized under the same load context. Only the timing observation endpoint changes.

### Characterization RC Units

`STAAdapter::queryWireResistance()` is treated as returning milliohms, consistent with the full-design RC paths. `CharBuilder` will convert resistance to ohms before installing temporary STA RC trees.

### Root Driver Boundary Closure

Root-driver compensation will explicitly model both sides of the root boundary:

- Root-driver Liberty lookup returns delay, power, and output slew.
- The physical root closure load is mapped to the capacitance lattice.
- The physical root closure load is also projected to one loaded root source branch for comparison with `HTreeTopologyChar::driven_cap_idx`.
- The root-driver output slew is mapped to the slew lattice.
- Each raw h-tree candidate is checked against the physical root closure buckets:
  - `entry.driven_cap_idx == physical root source-boundary bucket`
  - `entry.input_slew_idx == root-driver output slew bucket`

Strict closure is enabled only when root-driver sizing is enabled. When strict closure is active, it explicitly replaces the old raw `top_input_slew_covering_idx` feasible filter; `min_top_input_slew_ns` remains the root-driver Liberty input slew. The existing top-input-slew boundary filter remains available when strict root closure is not active. When strict closure rejects all candidates, the flow must report that failure distinctly instead of silently falling through.

### Root Driver Input Slew

Topology-side callers will populate `options.min_top_input_slew_ns` and source-trunk segment `min_input_slew_ns` from `CONFIG_INST.get_root_input_slew()`. The config default is `0.0`; this represents an ideal/no-lower-bound root input condition and must not be converted into a char slew lattice filter.

The old topology-side `max_buf_tran * 0.5` derivation is removed. Direct h-tree helper code may still retain its no-option fallback for callers that do not pass topology options, but the production topology flow uses the configured `root_input_slew` value. This value is only the input condition for the root-driver Liberty lookup. It is not the h-tree top input slew after the root driver; the latter comes from the queried root-driver output slew.

### Fanout And Routing Compatibility

The integration must preserve:

- topology `max_leaf_load_count`;
- `source_exposed_load_count`;
- branch and root fanout pruning;
- fanout=4 legality behavior;
- overlapping-terminal legalization before FLUTE.

Root boundary closure must run in the current pruning order and must not weaken the later fanout legality filter.

### Reporting

Reports will expose the root boundary state needed to diagnose semantic drift:

- root-driver input slew;
- root-driver output slew;
- root-driver output slew lattice bucket;
- raw h-tree char source cap bucket vs physical root source-boundary load bucket;
- physical root total load bucket used by root-driver compensation;
- raw h-tree char input slew bucket vs physical root output slew bucket;
- cap and slew bucket deltas.

Selected per-level buffer pressure may be reported if the required fields fit naturally into the existing `LevelPlan` structure.

## Rollout / Rollback

The change is limited to native iCTS h-tree and characterization paths. If strict boundary closure is too restrictive for a flow, rollback is straightforward:

- keep the char observation and RC unit fixes;
- disable strict root boundary filtering while retaining diagnostics;
- keep the existing fanout pruning and top-input-slew fallback behavior.

The final acceptance flow is:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Binary validation must be run for both fanout 4 and fanout 32, and the final report must include runtime, QoR, and evaluation STA error.
