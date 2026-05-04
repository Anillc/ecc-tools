# Fix the delay/power semantics of the h-tree root driver layer

## Goal

Fix the semantic gap in H-tree final delay/power metrics caused by root-level buffering. The current task is limited to researching and designing a compensation for the root driver's input-output cell arc delay and the corresponding power terms, under the assumption that the existing root buffering principle remains unchanged.

## What I Already Know

* H-tree `best_char.delay/power` currently represents the selected abstract topology metric from the characterization/frontier flow, not a post-embedding STA/iPW result.
* Existing characterization measures segment timing from a virtual source buffer output boundary to a virtual sink buffer input boundary.
* The root driver layer gap is specifically the root buffer input-output arc delay and related power terms that are outside the current H-tree topology metric.
* The user wants investigation before implementation.
* The desired solution should avoid unnecessary redundant computation.

## Assumptions

* The root buffering principle and selected root buffer policy should remain unchanged in this task.
* "Design-level delay/power" for this task means a compensated abstract metric, not a full final design STA/iPW signoff metric.
* The compensation should cover root cell delay and root-related cell/net power as far as can be derived without constructing a full RC tree.

## Research Questions

* Can root cell delay/power be estimated without invoking iSTA/iPW on a full RC tree, by querying Liberty tables with input slew and load cap?
* Is net power feasible without full RC tree construction, or does it require a model/approximation?
* At which stage should the extra compensation be injected so it affects candidate distribution without excessive redundant computation?

## Requirements

* Analyze available iCTS/iSTA/iPW APIs for direct Liberty delay/power lookup.
* Identify the minimal data needed for root driver compensation: input slew, output load cap, selected root cell, net load/wire cap if applicable.
* Propose where in the H-tree candidate flow to add the compensated delay/power.
* Keep this task focused on root-level buffering compensation only.

## Acceptance Criteria

* [ ] A research report explains whether direct Liberty lookup is feasible for cell delay and cell power.
* [ ] The report explicitly states limitations for root net power.
* [ ] The report proposes a concrete injection point in the current H-tree flow.
* [ ] The report lists files/functions likely affected by implementation.
* [ ] The report separates strict scope from out-of-scope full STA/iPW signoff work.

## Definition of Done

* Research notes are persisted under `research/`.
* `prd.md` is updated with the chosen approach after user confirmation.
* Implementation context files are curated before Phase 2.

## Out of Scope

* Changing the root buffering selection principle.
* Replacing H-tree characterization with full post-embedding STA/iPW.
* Changing source-to-root trunk semantics except where root driver compensation shares helper APIs.
* Full skew/path distribution modeling.

## Technical Notes

* Previous read-through identified H-tree final selection in `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc` and `topology_pruning/TopologyPruning.cc`.
* Previous read-through identified segment characterization in `src/operation/iCTS/source/module/characterization/`.
* Previous read-through identified STA adapter characterization APIs in `src/operation/iCTS/source/database/adapter/sta/`.

## Research Summary

Detailed notes are in `research/root-driver-delay-power-compensation.md`.

* Root cell delay can be computed without building a full iSTA RC tree by querying Liberty delay tables with root input slew and root output load cap.
* Root cell internal power can be estimated without full iPW graph construction by using the same Liberty internal-power table semantics that iPW uses, multiplied by the same characterization reference clock toggle.
* Root cell leakage can be added as an abstract Liberty leakage estimate; exact conditional leakage parity would require signal-probability context and is out of scope for the first fix.
* Exact physical root output net switching power cannot be obtained without post-embedding RC context. The abstract root output net switching power is already represented by `source_boundary_net_switch_power` in the selected H-tree char, so the initial compensation should not add root net switching power again.
* The compensation should be injected after topology entries are composed and sink-load-region filtered, but before per-depth best selection and before global feasible/candidate pools store refs.

## Updated Implementation Scope

The implementation phase now targets a commit-ready production shape, not a temporary experiment patch:

* Add a dedicated `htree/compensation` module for root-driver compensation. The main compensation implementation must live there rather than inside topology pruning.
* Keep root-driver compensation as an explicit pass over current H-tree candidate entries before selection/pruning decisions that consume delay/power metrics.
* Remove temporary direct-vs-char experiment code and any source changes that only existed to support experiments.
* Keep evaluation support only for the H-tree semantic that matters here: `h-tree root input pin -> h-tree leaf buffer output pin`.
* Simplify synthesis/evaluation logs so they report only the useful comparison:
  * compensation delay/power component;
  * raw H-tree char metric;
  * compensated H-tree metric (`char raw + compensation`);
  * evaluation STA metric for `h-tree root input pin -> h-tree leaf buffer output pin`;
  * the final difference between compensated metric and evaluation.
* Do not run `ecc_dev_tools` or other ecc checks in this implementation pass.

Production assumptions:

* Root output net power remains as currently represented by H-tree source-boundary switching.
* Root-driver compensation adds root cell delay plus root cell internal/leakage power only.
* Root-driver compensation uses `max_slew / 2` as root input slew.
* The production synthesis fix must be selection-aware: root-driver compensation during topology pruning must not use `entry.get_driven_cap_idx()` as the physical root driver output load.
* Because final routed RC does not exist before embedding, candidate selection must use a candidate-specific root-closure physical load estimator derived from the selected topology pattern, topology geometry, first real root-net terminals, and the same routing/unit-RC semantics used by evaluation as closely as available.
* Post-embedding/evaluation may report the true routed root-net load cap or arrival distribution for validation, but that value is not the primary selection-stage load source.

## Selection-Aware Root Load Fix Scope

The root cause identified during follow-up analysis is that the selected H-tree char's `driven_cap_idx` is a per-branch abstract boundary state used for composition joins. It is not the final root-driver physical output load. The parent segment normally performs the `load ~= 2 * child.driven_cap` closure during composition, but the final root has no parent segment. Therefore the compensation layer must add an explicit root closure.

Implementation requirements:

* Keep `driven_cap_idx` semantics unchanged for segment/H-tree composition and hash-join compatibility.
* Replace the root-driver compensation load source with a root-compensation-specific load resolver in the new `htree/compensation` module.
* Keep topology pruning focused on candidate construction/filtering/selection. It may call the compensation pass, but it must not own root closure traversal, routing estimate, Liberty direct lookup, or compensation cache implementation.
* The resolver must estimate the physical root output net load for each candidate entry:
  * Materialize the candidate topology pattern and inspect its level segment patterns.
  * Traverse from the topology root through pure-wire/unbuffered upper levels until the first actual buffer input terminals that would be connected to the root output net.
  * Include all first real root-net terminal pin caps.
  * Estimate root output net wire cap from the candidate terminal geometry using FLUTE/ClockSteinerTree-style routing when feasible; otherwise use the closest existing CTS route-length estimator with clear fallback logging.
  * Quantize only where the existing cache key/lattice contract requires it; direct Liberty lookup should receive the physical estimated cap in pF.
* Preserve direct compensation caching by keying on the resolved physical load bucket/value.
* Evaluation must use structured H-tree layout metadata where available and must not use `htree_edge_buf_` name-pattern matching for H-tree leaf output detection.
* Remove char-only root-driver comparison probes from production source unless they are required by the production compensation path.
* Report output should be compact. It should not include verbose per-cache/per-runtime experiment details unless needed to understand the final selected result.

Required final report:

1. Summarize the code cleanup: new compensation module, removed experiment code, and remaining evaluation support.
2. Explain the selected result using compact metrics: raw H-tree char, compensation component, compensated H-tree metric, and selected root physical load.
3. Compare final compensated H-tree delay against evaluation STA `h-tree root input pin -> h-tree leaf buffer output pin`; do not use terminal/cluster output semantics or `0.4+` arrival values as H-tree leaf output.
