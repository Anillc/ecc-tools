# CTS SDC Clock Tracing

## Goal

Add automatic CTS clock tracing that derives CTS synthesis target nets from SDC-declared clocks without requiring users to edit external SDC files or manually force one net through `use_netlist`.

The feature must preserve the current characterization performance model by avoiding full-design STA graph construction during CTS clock discovery.

## Background / Known Context

- Current CTS clock read path uses lightweight SDC clock declaration parsing, then materializes one configured or SDC-source net through `Wrapper::readClocks`.
- Some SDC files are variable-based, for example `scripts/design/ics55_dev/default.sdc` uses `set clk_port [get_ports $clk_port_name]` and passes `$clk_port` into `create_clock`.
- Some SDC files are direct, for example `scripts/design/ics55_huge_dev/constraints.sdc` uses `create_clock -name clock [get_ports clock]`.
- CTS cannot require changes to external SDC syntax or content.
- The huge design currently needs `use_netlist=ON` to manually map `clock -> n194404`; without tracing, CTS may materialize the top source port net or an unrelated clock-like net and miss most sequential CK sinks.
- Correct behavior is not to pick a single "best" clock-like net. Every net that is proven to belong to an SDC clock and meets CTS clock-target criteria must be synthesized.
- Full-design STA context is not acceptable in the default CTS clock discovery path because iSTA currently uses singleton/global mutable state, while characterization relies on char-only small STA graphs.
- The SDC-facing implementation should live under `src/operation/iCTS/source/database/adapter/sdc` so the boundary is visible as a CTS SDC adapter rather than STA timing behavior.
- "Do not execute SDC" means the CTS adapter must not invoke real STA Tcl commands, mutate STA constraints, or build full-design timing state. It may still need side-effect-free parsing/evaluation of the SDC/Tcl subset required to resolve variables, command substitution, and arithmetic used by clock declarations.

## Requirements

- CTS shall derive clock trace roots from SDC `create_clock` and `create_generated_clock` declarations.
- CTS shall support SDC Tcl variables and expressions that resolve into clock sources, including variable-based `get_ports` results.
- CTS shall not modify, rewrite, or require changes to user-provided SDC files.
- CTS shall not call full-design STA timing context refresh during clock target discovery.
- CTS SDC parsing code shall be placed under `src/operation/iCTS/source/database/adapter/sdc`.
- CTS SDC parsing shall be side-effect-free with respect to STA: it shall not source the SDC into iSTA's real `ScriptEngine`, shall not create `SdcConstrain` objects for STA timing analysis, and shall not mutate timing arcs.
- CTS shall parse/evaluate only the SDC subset required for clock tracing, rather than attempting to be a complete SDC implementation.
- CTS shall preserve object provenance from SDC collection commands such as `get_ports`, `get_pins`, `get_nets`, `get_clocks`, and `all_clocks` where needed for clock tracing.
- CTS shall record lightweight `set_case_analysis` information so clock mux traversal can be decided from SDC mode constraints.
- CTS shall trace from an SDC clock source through safe clock structures only: buffers, inverters, clock gates, generated-clock boundaries, and explicitly constrained clock muxes.
- CTS shall not traverse arbitrary combinational logic by default.
- CTS may traverse a clock-gate-like combinational cell only under strict safeguards: one clock-provenance input, non-clock inputs constrained or classifiable as enable/control, bounded depth, no loop revisit, and output net meeting CTS clock-target criteria.
- CTS shall stop at ambiguous muxes, unconstrained dividers, sequential data/Q paths, ordinary combinational logic, and detected combinational loops.
- CTS shall treat generated clocks as clock ownership boundaries. Downstream targets of a generated clock belong to the generated clock name, not silently to the master clock.
- CTS shall materialize all accepted `(sdc_clock_name, target_net_name)` pairs as CTS `Clock` objects.
- CTS shall allow multiple CTS target nets for the same SDC clock.
- CTS shall reject or report ambiguous cases where the same physical target net is reachable from multiple unrelated SDC clocks without SDC constraints resolving ownership.
- Existing `use_netlist` config shall remain available as a compatibility/manual override path, but the default desired behavior is automatic tracing.
- The implementation shall emit a clock trace report explaining accepted, rejected, ambiguous, and trace-through nets.
- The implementation shall emit an SDC clock ownership report that shows clock kind, master clock, SDC target nets, owned nets, and CTS target nets.
- The implementation shall emit a report for clock-like nets that directly drive clock sinks but are not owned by any SDC-declared clock; these nets are diagnostics only and must not be materialized as CTS clocks.

## Acceptance Criteria

- [ ] Variable-based SDC such as `scripts/design/ics55_dev/default.sdc` resolves the clock source type and name without requiring SDC edits.
- [ ] Direct SDC such as `scripts/design/ics55_huge_dev/constraints.sdc` resolves `[get_ports clock]` as a port seed, not as an already-final CTS target net.
- [ ] With manual `use_netlist` disabled, the huge design can discover downstream CTS target nets that are SDC-clock reachable and directly drive sequential CK or legal clock-boundary sinks.
- [ ] If multiple accepted target nets belong to the same SDC clock, all are passed to CTS synthesis instead of selecting only one.
- [ ] A virtual clock without a netlist source is reported as skipped for CTS target generation.
- [ ] Generated-clock declarations create separate clock ownership and do not merge generated-clock downstream targets into the master clock.
- [ ] Ambiguous clock muxes without usable `set_case_analysis` are reported and not silently traversed.
- [ ] The tracing algorithm cannot loop indefinitely on designs with combinational cycles.
- [ ] Characterization continues to use char-only STA small graphs and does not require full-design STA graph construction for clock discovery.
- [ ] `Clock Trace Overview` or equivalent report includes SDC clock name, clock kind, master clock, target net, status, dominance, target kind, direct sequential CK sink count, macro clock sink count, trace path, and reason.
- [ ] `SDC Clock Ownership Overview` or equivalent report shows each SDC clock's kind, master clock, SDC target nets, owned nets, CTS target nets, and accepted sink counts.
- [ ] `Unowned Clock-like Nets` or equivalent report lists direct clock-sink-driving nets without SDC ownership, including dominance, target kind, sink counts, and reason, while leaving CTS materialization limited to SDC-owned accepted targets.

## Out of Scope

- Editing, normalizing, or regenerating user SDC files.
- Replacing iSTA's full SDC engine with a new complete SDC implementation.
- Executing real STA Tcl SDC commands in the CTS clock tracing adapter.
- Reworking iSTA into multiple independent timing contexts.
- Using full-design STA graph construction as the default production clock tracing path.
- Arbitrary data-path combinational traversal.

## Research References

- VTR SDC command documentation for `create_clock`, `create_generated_clock`, and collection commands: https://docs.verilogtorouting.org/en/latest/vpr/sdc_commands/
- AMD UG903 case analysis documentation for `set_case_analysis` mode constraints: https://docs.amd.com/r/2021.2-English/ug903-vivado-using-constraints/Case-Analysis
- AMD UG903 generated clock documentation for generated clock ownership and derivation: https://docs.amd.com/r/2025.1-English/ug903-vivado-using-constraints/About-Generated-Clocks?contentId=_tF8LszmDSxlIHKc79yovw

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For this complex task, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
