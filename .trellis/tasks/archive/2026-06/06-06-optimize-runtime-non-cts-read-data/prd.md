# Optimize iSTA Liberty parse/link runtime

## Goal

Analyze the `ics55_dev` iCTS runtime distribution and identify why the early
`CTSReadData` stage is dominated by Liberty parsing/linking. The optimization
focus is iSTA's Liberty parser/linker implementation, not CTS source code.

The current hypothesis is that iSTA Liberty reader/linker algorithm overhead,
especially visitor dispatch and per-node data construction in
`LibParserCpp.cc`, makes Liberty link runtime too heavy for the current flow.
CTS is only the reproduction workload that exposes the bottleneck.

Temporary development principle for iSTA in this task: performance is the
priority, with preservation of existing functionality as a hard constraint.
"Minimal invasive" means keeping the change boundary local and reviewable; it
does not forbid replacing a local inefficient algorithm when behavior stays
equivalent.

The user-observed reproduction command is:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

## Confirmed Facts

- The dev script initializes flow/DB/lib/SDC/LEF/DEF, runs `run_cts`, then saves
  DEF/netlist and reports.
- A task-local baseline run reports CTS total runtime `43.883 s`.
- In that baseline, `read_data` is `32.656 s`, `synthesis` is `10.470 s`,
  `optimization` is `0.592 s`, `instantiation` is `0.097 s`, and `evaluation`
  is `0.054 s`.
- `read_data` accounts for about `74.42%` of CTS internal runtime in the
  baseline run.
- Glog timestamps isolate about `32.516 s` of the `32.656 s` `read_data` stage
  in two Liberty file load/link sequences:
  - H7CL load `2.737 s`, link `13.539 s`.
  - H7CR load `2.787 s`, link `13.453 s`.
- Liberty link alone is about `26.992 s`, or `82.65%` of `read_data`.
- perf children-mode samples show the dominant user-space stack under
  `icts::Wrapper::loadLibertyIfNeeded` is iSTA Liberty link:
  - `ista::LibertyReader::linkLib`: `50.97%`.
  - `ista::LibertyReader::visitPin`: `38.67%`.
  - `ista::LibertyReader::visitInternalPower`: `21.26%`.
  - `ista::LibertyReader::visitSimpleAttri`: repeated sub-hotspot.
- Older archived May runtime artifacts for similar `ics55_dev` CTS runs show
  `read_data` around `8.0-8.7 s` while synthesis was around `31-33 s`, so the
  current distribution is a regression or workload/path shift rather than the
  historical CTS bottleneck.
- CTS calls into Liberty lookup during SDC clock tracing, but the measured
  expensive work is iSTA Liberty parser/linker work, not clock-tree synthesis.
- iSTA already provides a normal Liberty read/link path through
  `Sta::readLiberty(...)`, `Sta::linkLibertys()`, and
  `LibertyReader::readLib()/linkLib()`.

## Requirements

- Preserve a reproducible runtime baseline under this task's artifacts.
- Quantify CTS internal runtime distribution from `cts.log`, including
  percentages of total CTS runtime.
- Compare the current distribution with existing archived CTS runtime artifacts
  where relevant.
- Attribute the `CTSReadData` bottleneck to the iSTA Liberty parse/link subpath
  using logs, source inspection, and non-invasive process sampling before any
  source edits.
- Research the core logic of Liberty parsing/linking in iSTA:
  - Raw Liberty parse entry.
  - Raw-to-object link entry.
  - `LibertyReader` group/attribute visitor flow.
  - Cell filtering behavior via `_build_cells`.
  - Known test coverage for Liberty reader behavior.
- Identify algorithmic overhead or blocking points in iSTA's Liberty reader,
  with file/line evidence and ranked hypotheses.
- Keep the temporary iSTA development principle explicit: performance-oriented,
  local, behavior-preserving changes; do not treat minimal invasiveness as a
  reason to keep an obviously expensive local algorithm.
- Produce a task-local analysis report with findings, evidence, likely root
  cause areas, and next optimization options.
- If implementation becomes necessary, create or update `design.md` and
  `implement.md` before starting the task. Do not edit CTS source code in this
  task unless the user explicitly changes scope.

## Acceptance Criteria

- [ ] The task has a saved baseline run or a saved explanation of why a new run
      could not be completed.
- [ ] Runtime distribution is reported with absolute seconds and percent of CTS
      total for every stage in the current CTS Runtime Overview.
- [ ] The analysis identifies Liberty parse vs Liberty link cost and explains
      why iSTA `LibertyReader::linkLib()` is the dominant subpath.
- [ ] The report documents Liberty parser/linker control flow and core visitor
      logic with source references.
- [ ] iSTA-side algorithmic bottleneck candidates are ranked by expected impact,
      invasiveness, and behavior risk.
- [ ] The first implementation slice, if approved, is scoped as a
      performance-oriented iSTA-only change with local boundaries and preserved
      Liberty reader functionality.
- [ ] Worktree status is reported.

## Out of Scope

- Changing CTS synthesis, H-tree search, characterization, optimization, or QoR
  behavior in this analysis step.
- Editing CTS source code for this task.
- Broad rewrites of iDB, iSTA, or Liberty internals.
- Changing Liberty parser semantics, supported syntax, or parsed data model.
- Reducing configured Liberty file lists or dropping timing/power content to
  win runtime unless separately approved.
- Treating full-flow setup time before `run_cts` as CTS runtime unless it is
  explicitly separated from the CTS Runtime Overview.

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
