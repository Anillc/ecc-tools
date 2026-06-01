# Optimize CTS architecture

## Goal

Improve the iCTS architecture so that its runtime boundary, entry API,
configuration contract, build dependencies, and flow facades are explicit and
maintainable without changing CTS algorithms or expected design output.

The immediate outcome is a reviewable refactor plan that can be implemented in
small, verifiable steps. The implementation should make CTS failures observable
to Tcl/Python callers, remove stale or misleading build/config contracts, and
reduce architecture drift in the CTS flow layer.

## Background

iCTS already has a recognizable architecture:

- `api/` exposes `CTSAPI` as the external singleton boundary.
- `source/database/` owns runtime data structures and adapters.
- `source/flow/` owns the pipeline: setup, synthesis, optimization,
  instantiation, evaluation, and report.
- `source/flow/synthesis/` has readable topology and HTree facades.

The current quality issues are not primarily algorithmic. They are boundary and
contract issues:

- API and tool-manager calls can report success even when CTS fails.
- Tcl/Python/report paths do not consistently use the same boundary.
- Runtime config, Tcl config, and sample JSON contain drift such as
  `use_netlist` and `net_list`.
- The product decision is to clean up/deprecate `use_netlist` and `net_list`
  rather than preserve a net-list based CTS input mode.
- Some CMake targets still expose broad include roots or stale legacy include
  lists.
- Flow, synthesis, and topology entry files are growing into fat facades.

## Requirements

- Preserve current CTS behavior and output unless a specific bug fix is called
  out in the implementation plan.
- Keep `CTS_API_INST` as the only CTS singleton boundary. Do not introduce a new
  service locator or deep runtime singleton.
- Make setup, run, and report status explicit and propagate failures through
  `CTSAPI`, `CtsIO`, tool-manager, Tcl, and Python entry points.
- Keep runtime-owned dependencies (`Config`, `Design`, `Wrapper`, `FastSTA`,
  `SchemaWriter`) bound at API/flow boundaries. Lower-level algorithms should
  receive narrow input structs, policies, or references.
- Resolve config contract drift between Tcl options, JSON samples, and
  `Config::parse`.
- Treat `use_netlist` and `net_list` as deprecated/unused CTS config items.
  Remove them from active examples and command surfaces where feasible, and
  warn-and-ignore them if they are still encountered in user config.
- When parsing CTS config, any key that is not part of the current supported
  config contract must produce a consistent warning:
  - unknown key: invalid config key;
  - known deprecated key: this item is no longer used.
- Tighten iCTS CMake/include contracts incrementally. Use target dependencies,
  prefer `PRIVATE`, expose `PUBLIC`/`INTERFACE` only when public headers require
  it, and avoid link groups or duplicate archive workarounds.
- Delete or update stale legacy include declarations that no longer describe the
  current CTS tree.
- Split fat facades only where it reduces real coupling or isolates a stable
  contract. Keep established readable names such as setup, synthesis,
  optimization, instantiation, evaluation, report, topology, and htree.
- Maintain the existing source layering: API may depend on Source; Test may
  depend on API/Source; Source must not depend on API.
- Add or update focused tests for API status propagation, config parsing, and
  entry-path behavior.

## Non-Goals

- Rewriting HTree, topology, optimization, routing, or STA algorithms.
- Broadly redesigning tool-manager singletons outside the CTS integration
  surface.
- Renaming the established CTS flow taxonomy.
- Solving unrelated py/ecc packaging or removed-tool issues.
- Removing every aggregate CMake target in one pass if that is not needed for
  the CTS architecture goal.

## Acceptance Criteria

- [ ] `CTSAPI` exposes a typed status/result contract for init, run, and report
      operations.
- [ ] `CtsIO`, tool-manager, Tcl, and Python callers propagate CTS failures
      instead of unconditionally reporting success.
- [ ] Direct Tcl/Python report paths are either routed through the same boundary
      as auto-run or explicitly documented and status-checked.
- [ ] `use_netlist` and `net_list` are removed/deprecated consistently across
      Tcl config, sample JSON, docs/comments, and runtime parsing.
- [ ] Unknown CTS config keys produce a consistent warning for invalid config
      key.
- [ ] Deprecated CTS config keys, including `use_netlist` and `net_list`,
      produce a consistent warning that the item is no longer used and are
      ignored.
- [ ] Invalid present numeric config values are diagnosed instead of silently
      defaulting. Missing optional values may still use documented defaults.
- [ ] Stale `cmake/operation/icts.cmake` content is removed or updated to match
      the current iCTS architecture.
- [ ] Production iCTS CMake targets expose narrower include roots and target
      dependencies where feasible, without reintroducing duplicate archive
      sensitivity.
- [ ] Source-layer code does not depend on API and no new singleton/service
      locator is introduced.
- [ ] Flow/synthesis/topology facade cleanup, if performed, keeps one readable
      public facade per behavior area and moves only cohesive implementation
      details behind it.
- [ ] Existing iCTS tests continue to pass, and new focused tests cover the
      changed contracts.
- [ ] `cmake --build build --target ecc_bin -j 32` passes.
- [ ] Focused iCTS `ctest` targets pass.
- [ ] `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
      reports zero in-scope findings or all findings are documented with a
      follow-up decision.
- [ ] `git diff --check` passes.

## Decisions

- `use_netlist` and `net_list` will be cleaned up/deprecated instead of
  implemented end-to-end.
- Unknown config keys warn as invalid config keys.
- Known deprecated config keys warn that the item is no longer used.

## Notes

- This task is in planning. Do not start implementation until the work plan is
  accepted.
