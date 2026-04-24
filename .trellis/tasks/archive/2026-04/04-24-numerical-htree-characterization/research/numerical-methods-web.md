# Research: numerical methods for H-tree characterization

- Query: Mathematical replacement strategies for enumerative timing/clock-tree characterization, including response-surface modeling, polynomial regression for NLDM-like delay/slew/power surfaces, affine/quadratic composition, dynamic programming versus continuous/discrete optimization, and convex/quadratic or mixed-integer formulations for clock-tree/H-tree sizing, buffering, and pattern selection.
- Scope: mixed
- Date: 2026-04-24

## Findings

### Files Found

- `src/operation/iCTS/source/database/characterization/CharCore.hh` - compact electrical boundary and cost carrier for characterization entries.
- `src/operation/iCTS/source/database/characterization/SegmentChar.hh` - segment-level characterization entry and additive segment composition.
- `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh` - H-tree-level characterization entry and binary-fanout power composition.
- `src/operation/iCTS/source/database/characterization/BufferingPattern.hh` - segment pattern metadata: normalized buffer positions, cell masters, terminal branch buffer state, and monotonic boundary rules.
- `src/operation/iCTS/source/database/characterization/HTreeTopologyPattern.hh` - compact H-tree pattern metadata storing level-to-segment pattern references.
- `src/operation/iCTS/source/database/characterization/ValueLattice.hh` - uniform slew/cap/length lattice used to convert physical values to bins.
- `src/operation/iCTS/source/module/characterization/CharBuilder.hh` - segment characterization builder API, grids, samples, and output vectors.
- `src/operation/iCTS/source/module/characterization/CharBuilder.cc` - enumerates segment buffer topologies and queries iSTA/iPA for delay, output slew, and power samples.
- `src/operation/iCTS/source/module/characterization/HashJoinEngine.hh` - generic hash-join concatenation engine and optional Pareto/state pruning.
- `src/operation/iCTS/source/module/characterization/SegmentTraits.hh` - segment hash-join key and compose traits.
- `src/operation/iCTS/source/module/characterization/HTreeTraits.hh` - H-tree hash-join key, half-cap transform, and compose traits.
- `src/operation/iCTS/source/module/characterization/Frontier.hh` - boundary-aware frontier grouping and delay/power dominance pruning.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh` - H-tree build options, selected level plans, candidate summaries, and returned artifacts.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` - end-to-end H-tree flow: topology, characterization, segment frontier synthesis, H-tree composition, actual-load filtering, Pareto selection, and materialization.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh` - characterization-facing iSTA/iPA facade for wire RC, Liberty limits, timing, slew, and power sampling.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` - source slew injection, Liberty timing arc lookup, timing query, power setup, and Liberty port/cap queries.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc` - real-tech H-tree smoke test and opt-in ARM9 full-sink experiment matrix.
- `.trellis/tasks/04-24-numerical-htree-characterization/prd.md` - task requirements and constraints for isolated numerical characterization/H-tree modules.
- `.trellis/spec/backend/directory-structure.md` - source/module/flow placement and CMake target rules.
- `.trellis/spec/backend/quality-guidelines.md` - iCTS C++ naming, include, and validation guidance.
- `.trellis/spec/project-constraints.md` - repository-wide iCTS constraints, file naming, logging, and validation policy.

### Current iCTS Enumeration and Composition Patterns

- `CharCore` stores the current characterization state as four discrete boundary indices plus delay, power, pattern id, and source-boundary switching power. The model replacement should preserve these boundary semantics even if fitted models use physical `slew_in_ns` and `cap_load_pf` internally (`CharCore.hh:41`, `CharCore.hh:55`, `CharCore.hh:59`).
- `SegmentChar::compose` is algebraically simple: source-side `input_slew_idx` and `driven_cap_idx` come from the upstream entry, sink-side `output_slew_idx` and `load_cap_idx` come from downstream, delay is additive, and power subtracts the downstream source-boundary switching power to avoid double counting (`SegmentChar.hh:60`, `SegmentChar.hh:77`, `SegmentChar.hh:83`).
- `HTreeTopologyChar::compose` follows the same boundary propagation but multiplies the downstream contribution by two for binary fanout: `up.power + 2 * (down.power - down.source_boundary_net_switch_power)` (`HTreeTopologyChar.hh:63`, `HTreeTopologyChar.hh:83`, `HTreeTopologyChar.hh:90`).
- `HTreeTraits` encodes the H-tree fanout load transform outside the compose function: the upstream probe key uses `ceil(load_cap_idx / 2)` to match the downstream driven-cap bin (`HTreeTraits.hh:32`, `HTreeTraits.hh:43`, `HTreeTraits.hh:57`).
- `UniformValueLattice` uses a uniform grid and `ceil`-based covering indices. Existing `SegmentChar` comments explicitly assume `value = idx * step_size`; numerical models should retain a reversible physical-domain mapping so fitted values can be compared against existing binned data (`ValueLattice.hh:47`, `ValueLattice.hh:60`, `ValueLattice.hh:62`; `SegmentChar.hh:37`).
- `CharBuilder` currently has the exact data source needed for regression: physical grids in `_slews_to_test` and `_loads_to_test`, the current physical `output_slew_ns`, `driven_cap_pf`, `delay_ns`, `power_w`, and `source_boundary_net_switch_power_w` before conversion into binned `CharCore` entries (`CharBuilder.hh:164`, `CharBuilder.cc:968`, `CharBuilder.cc:982`, `CharBuilder.cc:1037`, `CharBuilder.cc:1040`, `CharBuilder.cc:1042`, `CharBuilder.cc:1058`).
- The current segment pattern explosion starts from all buffer-position bitmasks over slots and monotonic combinations of sorted buffers. Pattern count is therefore exponential in slots and combinatorial in buffer choices: `num_topologies = 1 << num_slots`; each nonempty topology contributes `getMonotonicComboCount(buffer_count, positions)` (`CharBuilder.cc:687`, `CharBuilder.cc:693`, `CharBuilder.cc:708`, `CharBuilder.cc:714`, `CharBuilder.cc:746`).
- `CharBuilder::characterizeTopology` creates a concrete char-only circuit for each pattern and performs nested `load_pf` then `input_slew_ns` sampling through `STA_ADAPTER_INST`, which is exactly the expensive path the numerical method is meant to reduce (`CharBuilder.cc:909`, `CharBuilder.cc:955`, `CharBuilder.cc:968`, `CharBuilder.cc:1017`, `CharBuilder.cc:1030`).
- `HashJoinConcat` builds a downstream hash table keyed by boundary indices, probes with upstream keys, composes every match, and optionally prunes dominated frontier entries within state groups. The average complexity is documented as `O(|U| + |D| + J)` where `J` is match count, but `J` still grows with enumerated pattern pairs (`HashJoinEngine.hh:73`, `HashJoinEngine.hh:95`, `HashJoinEngine.hh:119`, `HashJoinEngine.hh:180`, `HashJoinEngine.hh:195`).
- `Frontier.hh` prunes only by delay/power dominance within exact boundary and pattern-composition-state groups. This is compatible with numerical models if fitted expressions are evaluated to scalar delay/power at the same comparison points, but the grouping keys are still discrete (`Frontier.hh:182`, `Frontier.hh:197`, `Frontier.hh:255`, `Frontier.hh:280`).
- `HTreeBuilder` already has a dynamic-programming-like closure for required length bins. It recursively decomposes a target length index into left/right lengths, memoizes pending-length states, and chooses the best feasible closure by total decomposition cost and frontier size (`HTreeBuilder.cc:1020`, `HTreeBuilder.cc:1087`, `HTreeBuilder.cc:1110`, `HTreeBuilder.cc:1152`).
- H-tree composition is bottom-up by levels: for each planned level, select the length-index frontier, make seed H-tree entries from segment entries, then hash-join the seed entries with the downstream frontier (`HTreeBuilder.cc:1669`, `HTreeBuilder.cc:1695`, `HTreeBuilder.cc:1712`, `HTreeBuilder.cc:1730`, `HTreeBuilder.cc:1739`).
- Final H-tree selection is a delay/power Pareto-front policy followed by power-median ordering and tie-breaks on driven cap, output slew, delay, power, load cap, input slew, and pattern id (`HTreeBuilder.cc:823`, `HTreeBuilder.cc:857`, `HTreeBuilder.cc:875`, `HTreeBuilder.cc:1810`, `HTreeBuilder.cc:1830`).
- Actual-load legality is not just model algebra. `HTreeBuilder` rechecks real load groups, fanout, lower-bound electrical legality, exact routed cap, and required leaf load cap coverage. A numerical flow must keep this filter or intentionally report that a model-selected pattern failed actual-load coverage (`HTreeBuilder.cc:1456`, `HTreeBuilder.cc:1483`, `HTreeBuilder.cc:1501`, `HTreeBuilder.cc:1532`, `HTreeBuilder.cc:1561`, `HTreeBuilder.cc:1644`).
- The ARM9 full-sink test infrastructure is opt-in via environment variables and already writes matrix reports with runtime, success, frontier count, selected depth, delay, power, and characterization-grid diagnostics. It is the natural comparison target for a numerical H-tree flow (`HTreeBuilderRealTechSmokeTest.cc:82`, `HTreeBuilderRealTechSmokeTest.cc:95`, `HTreeBuilderRealTechSmokeTest.cc:125`, `HTreeBuilderRealTechSmokeTest.cc:480`, `HTreeBuilderRealTechSmokeTest.cc:523`, `HTreeBuilderRealTechSmokeTest.cc:562`).

### External References

- Liberty/NLDM authoritative baseline: the Liberty Reference Manual defines timing lookup templates using `input_net_transition` and `total_output_net_capacitance`, with `index_1` and `index_2` as floating-point axes; it also defines `cell_rise` as a cell delay lookup table in CMOS nonlinear timing models. It similarly supports internal power lookup templates indexed by input transition and output capacitance, plus polynomial coefficient syntax for some power/capacitance/current groups. Sources: Liberty Reference Manual 2007.03, Berkeley mirror, lines 1251-1274, 2521-2533, 2606-2640, 8575-8627, 8889-8909, 10783-10823: https://people.eecs.berkeley.edu/~alanmi/publications/other/liberty07_03.pdf
- Response-surface methodology baseline: NIST/SEMATECH describes response surfaces as main effects, interactions, and optional quadratic/cubic terms, and notes that quadratic models are normally the industrial workhorse when factor bounds are chosen correctly. For this task's two variables, the natural degree-2 design row is `[1, slew, cap, slew*cap, slew^2, cap^2]`; degree-1 is `[1, slew, cap]`. Source: NIST Engineering Statistics Handbook, response surface designs, lines 20 and 36: https://www.itl.nist.gov/div898/handbook/pri/section3/pri336.htm
- Timing models as functions rather than tables: ASP-DAC 2003 short-circuit-power work states that typical timing rules formulate output delay and output slew as functions of input slew and load capacitance, and that these functions have traditionally been either polynomial functions or multidimensional tables. This directly supports an NLDM-like polynomial replacement for iCTS sparse samples. Source: A. B. Kahng et al., "Predicting Short Circuit Power From Timing Models," ASP-DAC 2003, lines 35-50: https://www.cecs.uci.edu/~papers/compendium94-03/papers/2003/aspdac03/pdffiles/03c_2.pdf
- Polynomial delay/slew examples: Lee, Hong, and Lim's slew-aware buffer insertion paper compares linear gate delay/slew to k-factor equations and then adopts higher-order polynomials in effective cap/load and input slew for better fit. It also documents why slew-aware DP weakens simple dominance relations. Source: "Slew-Aware Buffer Insertion for Through-Silicon-Via-Based 3D ICs," CICC 2012, lines 217-293 and 306-316: https://gtcad.gatech.edu/www/papers/cicc12.pdf
- CTS with aggressive buffer insertion: Chen, Dong, and Chen integrate topology generation, buffer insertion, buffer sizing, and accurate delay/slew estimation for robust slew control. They emphasize the same bottom-up difficulty this task faces: upstream input slew is unknown while downstream delay and slew are sensitive to it. Source: "Clock Tree Synthesis under Aggressive Buffer Insertion," DAC 2010, lines 5-15 and 57-78: https://dchen.ece.illinois.edu/research/clocktree_dac10.pdf
- Dynamic programming baseline: IBM's ICCD 2000 buffer-library-selection paper describes Van Ginneken's dynamic-programming algorithm as a prevalent buffer-insertion technique and notes its quadratic complexity pressure. Li and Shi later summarize classic Van Ginneken as `O(n^2)` over candidate buffer positions and the Lillis extension as `O(b^2 n^2)` for `b` buffer types, then improve to `O(bn^2)` via convex-hull structure. Sources: IBM Research, "Buffer library selection," lines 6-9: https://research.ibm.com/publications/buffer-library-selection and Li/Shi arXiv page lines 35-43: https://arxiv.org/abs/0710.4691
- Hybrid DP/QP baseline: Mo and Chu combine quadratic programming for a wire branch with dynamic programming for an interconnect tree, with discrete wire widths and buffer sizes. This is a close analog for the iCTS design split: DP over tree/level structure, QP-like or fitted algebra inside each segment. Source: "Hybrid Dynamic/Quadratic Programming Algorithm for Interconnect Tree Optimization," IEEE TCAD 2001, lines 0-5 and 36-41: https://class.ece.iastate.edu/vlsi2/docs/Papers%20Done/2001-5-TCAD-FY.pdf
- Convex QP for wire/buffer sizing: Chu and Wong formulate simultaneous buffer insertion/sizing and wire sizing as a convex quadratic program for a wire under Elmore delay, with extensions for buffer sizing and objectives/constraints such as area or power. This supports using continuous/quadratic optimization when the model can be kept convex and topology is fixed. Source: "A Quadratic Programming Approach to Simultaneous Buffer Insertion/Sizing and Wire Sizing," IEEE TCAD 1999, lines 16-26, 78-87, and 173-180: https://class.ece.iastate.edu/vlsi2/docs/Papers%20Done/1999-6-TCAD-CC.pdf
- Mixed-integer clock-buffer formulation precedent: Huang and Cheng propose a mixed-integer linear programming approach for power-mode-aware buffer synthesis and clock-skew minimization with a global latency value. This supports MILP/MIQP as a valid formulation family for discrete clock-buffer pattern decisions, although the problem solved is different from this H-tree pattern-selection task. Source: IEICE Electronics Express article page, lines 90-149: https://www.jstage.jst.go.jp/article/elex/13/14/13_13.20160511/_article
- Integrated DME/buffer/wire sizing precedent: Liu, Chou, Aziz, and Wong's ISPD 2000 IDME work introduces wire widths and buffer levels as variables in deferred-merge embedding, producing zero-skew trees by construction while reducing phase delay, wire length, and buffer count. This is useful background for "optimization over H-tree construction variables" rather than post-processing enumeration. Source: Illinois Experts page, lines 94-107: https://experts.illinois.edu/en/publications/zero-skew-clock-tree-construction-by-simultaneous-routing-wire-si/
- Convex optimization reference: Boyd and Vandenberghe's textbook is the standard reference for affine constraints, quadratic objectives, convexity, and KKT/dual reasoning. For this task it is best used as a formulation sanity check: quadratic response surfaces only lead to convex QP when fitted Hessians and constraints satisfy convexity; arbitrary polynomial timing fits do not. Source: Stanford book page: https://stanford.edu/~boyd/cvxbook/
- iCTS paper context: The DAC 2024 iCTS paper and TCAD 2025 article frame the existing tool around hierarchical CTS, SLLT, clustering/topology/routing/buffering/optimization, and buffer evaluation using load capacitance and input slew. This research task is consistent with that direction but targets the local C++ H-tree characterization path. Sources: DAC 2024 PDF, lines 0-35 and 37-38: https://www.cse.cuhk.edu.hk/~byu/papers/C212-DAC2024-iCTS.pdf; IEEE Xplore DOI page: https://ieeexplore.ieee.org/document/10916748/

### Mathematical Strategy Implications

- A sparse initial characterization can fit independent response surfaces per fixed segment pattern and metric:
  - `delay_ns = f_delay(slew_in_ns, cap_load_pf)`
  - `slew_out_ns = f_slew(slew_in_ns, cap_load_pf)`
  - `power_w = f_power(slew_in_ns, cap_load_pf)`
  - optionally `driven_cap_pf = f_driven_cap(cap_load_pf)` for unbuffered patterns or a constant/length-derived value for buffered patterns.
- For degree-2 fits, at least six non-collinear samples are required per metric. A 3x3 grid is a practical minimum because it estimates curvature and leaves residual degrees of freedom for RMSE/R2; 2x2 can only fit affine plus interaction terms reliably and cannot identify both pure quadratic terms.
- Normalize variables before fitting, e.g. `x = (slew - slew_mid) / slew_range`, `y = (cap - cap_mid) / cap_range`. This makes coefficients comparable, improves conditioning, and makes ridge regularization meaningful. Store the normalization in the model so physical units remain exact at the API boundary.
- Quality metrics should be persisted per pattern and metric: `n`, residual sum of squares, `RMSE`, `max_abs_error`, `R2`, adjusted `R2` where useful, and a flag for underdetermined or rank-deficient fits. Leave-one-out or holdout error is useful because the requested sparse grids can overfit.
- The current binned `SegmentChar` data loses exact physical `output_slew_ns` and `driven_cap_pf` after `tryMakeStoredSampleIndices`; reconstructing them as `idx * step` is usable for a first prototype but will inject quantization error. A higher-quality adapter should capture the pre-binning physical sample tuple inside the new numerical module boundary while keeping existing `CharBuilder` behavior unchanged or minimally touched only at an integration seam.
- Affine composition is closed under substitution: if `slew_out = a0 + a1*s + a2*c` and downstream delay is affine in downstream input slew/load, composed delay remains affine in the upstream variables plus downstream load.
- Quadratic composition is not generally closed under direct substitution when the downstream model is quadratic in a quadratic upstream slew; degree can grow to four. Practical options are:
  - keep an expression DAG and evaluate numerically without truncation;
  - reproject/refit the composed expression back to degree 2 over the expected `(slew, cap)` domain;
  - restrict slew propagation models to affine while allowing delay/power to be quadratic;
  - perform composition on sampled collocation points and refit after each level.
- iCTS's existing H-tree composition algebra gives a clear numerical recurrence. For one level:
  - `delay_total = delay_up + delay_down`
  - `power_total = power_up + fanout * (power_down - boundary_switch_power_down)`, with fanout `2` in current H-tree composition.
  - boundary cap relation follows `downstream_driven_cap ~= upstream_load_cap / 2`, matching `HTreeTraits::halfCapKey`.
- The binary fanout/cap relation can be represented continuously as `cap_down_driven = cap_up_load / 2` for model evaluation, then binned only when reporting compatibility with native `HTreeTopologyChar`.
- For pattern selection, a pure continuous QP is attractive only if variables are continuous buffer positions/sizes and the objective/constraints are convex. The current C++ implementation's materializer expects discrete `BufferingPattern` objects, so a continuous optimizer cannot be used directly without a later pattern-realization step.
- A low-risk first numerical optimizer is a discrete DP over a reduced pattern set:
  - fit surfaces per seed pattern from sparse char samples;
  - score each pattern at required per-level physical length/load/slew scenarios;
  - retain top-K patterns per level/boundary state;
  - compose/evaluate only top-K frontier expressions instead of all hash-join pairs;
  - select by the existing delay/power Pareto policy and actual-load legality filter.
- A MILP/MIQP formulation is feasible for top-K discrete pattern selection:
  - binary `z_{level,pattern}` chooses one segment pattern per H-tree level;
  - constraints `sum_p z_{l,p} = 1`;
  - compatibility constraints encode boundary slew/load/cap feasibility either through precomputed allowed arcs or big-M inequalities;
  - linear objective can minimize weighted delay/power evaluated at representative points;
  - quadratic objective can penalize skew/load mismatch or fit residual risk.
  This is useful for a later exact/discrete optimizer, but it is likely overkill for the first C++ module unless a solver dependency already exists.
- A hybrid DP/QP design best matches the literature and the local codebase: use DP for tree levels, length closure, and discrete pattern identity; use fitted affine/quadratic surfaces for the expensive timing/power evaluation previously done by repeated iSTA/iPA calls.

### Mapping to the Requested C++ iCTS Implementation

- Place reusable fit/model code under `src/operation/iCTS/source/module/numerical_characterization` per the PRD and backend directory rules. Keep it independent of `flow/htree` and avoid touching existing `module/characterization` unless a narrow sample adapter is proven necessary.
- Suggested module types:
  - `NumericalSample`: `{PatternId, length_idx, length_um, slew_in_ns, cap_load_pf, delay_ns, slew_out_ns, power_w, driven_cap_pf, source_boundary_net_switch_power_w}`.
  - `Polynomial2D`: normalized variables, basis enum (`kAffine`, `kQuadratic`), coefficient vector, domain bounds, `evaluate()`.
  - `FitMetrics`: `sample_count`, `rank`, `rmse`, `r2`, `max_abs_error`, status.
  - `PatternResponseModel`: per-pattern fitted `delay`, `slew_out`, `power`, `driven_cap`, and boundary/pattern metadata.
  - `NumericalCharLibrary`: lookup by `PatternId` and length/level constraints; emits inspectable fit reports.
- Keep the fit API physical-unit first. Convert to existing lattice indices only when comparing against native `SegmentChar`/`HTreeTopologyChar` or when writing compatibility reports.
- For least-squares fitting, the implementation does not need a heavy solver for the first version. Degree-2 two-variable regression can solve the normal equations or QR/SVD through a small local dense linear algebra helper. If no existing matrix utility is present in iCTS, a compact Gaussian/QR implementation scoped to small matrices is acceptable; avoid introducing a global dependency unless implementation reveals an existing project-wide linear algebra target.
- The first flow under `src/operation/iCTS/source/flow/numerical_htree` can be a deterministic comparison flow:
  - build/load sparse initial samples from existing `CharBuilder` for a reduced grid;
  - fit per-pattern models and report model-quality metrics;
  - build H-tree level plans using the same length/grid concepts as `HTreeBuilder`;
  - evaluate top-K pattern candidates per level using fitted surfaces;
  - compose/evaluate candidate H-tree QoR with existing fanout/power semantics;
  - run actual-load legality checks or an equivalent adapter before declaring success;
  - write a comparison artifact mirroring ARM9 matrix fields: native runtime/QoR, numerical runtime/QoR, selected depth, selected segment patterns, deltas, fit metrics.
- If direct reuse of `HTreeBuilder` private helpers becomes tempting, do not copy large private code blocks into the new flow. Either keep the first numerical test at the module level or add narrow shared helpers later after the implement agent proves a compile/test integration point is unavoidable.
- Acceptance tests should separate:
  - pure fitting tests with synthetic affine/quadratic surfaces and known coefficients;
  - composition tests that assert additive delay and H-tree binary-fanout power semantics;
  - optimizer tests that verify top-K/DP selection against a tiny enumerated reference;
  - ARM9 real-tech comparison that skips when realtech prerequisites are unavailable and writes artifacts.

## Caveats / Not Found

- I did not find an authoritative source saying "quadratic is always accurate for iCTS segment delay/slew/power." The NIST guidance supports quadratic response surfaces as an industrial default, and VLSI timing literature supports polynomial/table timing functions, but the task's own sample data must decide whether affine or quadratic is sufficient.
- Convex QP results from the literature depend on simplified Elmore-style delay and convex problem structure. Fitted delay/slew/power response surfaces can be nonconvex; arbitrary quadratic coefficients do not automatically produce a convex optimization problem.
- Mixed-integer formulations are well supported by CTS/buffer literature for related problems, but a full MILP/MIQP solver dependency may conflict with the PRD's isolation and runtime goals. A top-K discrete DP is the lower-risk first implementation.
- Current `CharBuilder` stores exact physical delay and power but only stores output slew/driven cap as binned indices in `SegmentChar`. A production-grade numerical fit should capture exact physical sample tuples before binning; reconstructing from `UniformValueLattice` is a quantized fallback.
- Exact pattern identity with the native enumerative flow is not required by the PRD. The research supports optimizing for QoR/runtime parity and inspectable deltas rather than matching the same `PatternId`.
