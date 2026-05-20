# CTS convergence implementation plan

## Current Phase

Implementation and validation are complete for the source-structure convergence scope. The task remains unarchived because the user has not asked
for a commit or archive.

## Execution Rules

- Do not commit or archive unless the user explicitly asks.
- Do not revert unrelated dirty worktree changes.
- Source cleanup comes first; test cleanup follows after source structure is stable.
- Keep runtime behavior unchanged unless a behavior-affecting change is explicitly identified.
- Use child tasks or checklist sections for independently verifiable work, but keep this parent as the source of the convergence requirements.
- Mark checklist items complete only after their validation passes.

## Checklist

### 1. Planning and audit

- [x] Mark previous parent task completed.
- [x] Create convergence task.
- [x] Record initial audit evidence in `research.md`.
- [x] Define initial directory external-contract rule in `design.md`.
- [x] Record implementation sequence in `implement.md`.
- [x] Review open questions with the user before source implementation starts.

### 2. FastSTA root contract

- [x] Audit all production includes of FastSTA helper headers.
- [x] Decide which FastSTA helper types are true public CTS contracts and which are implementation details.
- [x] Move implementation-only FastSTA helper headers out of the root or into subfolder-local headers.
- [x] Expose required public FastSTA types through `FastSta.hh` or stable domain-data headers.
- [x] Update `flow/optimization` callers to depend on the narrowed FastSTA contract.
- [x] Update `module/characterization` callers to avoid direct FastSTA helper headers.
- [x] Adjust FastSTA CMake so implementation subfolder include paths are not broad public include surfaces.
- [x] Validate root directory contains only `FastSta.hh`, `FastSta.cc`, and `CMakeLists.txt`.
- [x] Build focused FastSTA targets.

### 3. `database/io` naming correction

- [x] Remove root-visible `ClockIdbWritebackData.hh`.
- [x] Remove root-visible `ClockIdbPinMembership.cc`.
- [x] Fold writer-only helpers into `WrapperClockWriter.cc` where feasible.
- [x] Confirm writer helpers do not need a separate visible root helper file.
- [x] Remove public type names containing `Writeback` and `Membership`.
- [x] Build focused database/io targets.

### 4. Primary large-directory convergence

- [x] Split `module/characterization` by builder, circuit, sampling, pattern, table/pruning, and buffer-cell responsibility.
- [x] Keep only `Characterization.hh/.cc` as the characterization root external contract.
- [x] Remove direct external includes of characterization internals where possible.
- [x] Split `module/routing/bound_skew_tree` by router entry, tree algorithm, geometry, components, conversion, and config.
- [x] Keep only `BSTRouter.hh/.cc` as the bound-skew routing root external contract.
- [x] Remove direct external includes of BST geometry/components where possible.
- [x] Build focused characterization and routing targets.

### 5. Secondary directory convergence

- [x] Audit `flow/synthesis/htree/analytical_solver` and split if root still exposes solver internals.
- [x] Audit `database/adapter/sdc` and keep only `SdcClockReader.hh/.cc` as the root external contract.
- [x] Audit `module/topology/fast_clustering` and move working data/helpers out of root if needed.
- [x] Audit `database/adapter/sta` and hide adapter-internal timing-query helpers if needed.
- [x] Audit `flow/synthesis/htree/solution` for structure and naming after H-tree folders settle.
- [x] Record accepted data-model exceptions for `database/design`, `database/characterization`, and any similar directories.
- [x] Remove redundant `module/routing/database` forwarding target and link `icts_source_database_routing` directly.
- [x] Remove `.md` files under `src/operation/iCTS`.

### 6. Final new-file naming review

- [x] Generate current untracked/renamed file inventory.
- [x] Classify pure moves separately from true new files.
- [x] Produce a user-facing list of true new source files and uncertain moved-and-edited names.
- [x] Ask the user for naming direction before applying uncertain renames.
- [x] Apply approved names and update includes/CMake.
- [x] Scan iCTS source for banned structural terms.

### 7. Final validation

- [x] Run focused builds for modified targets.
- [x] Run focused iCTS tests affected by source moves.
- [x] Run `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.
- [x] Run the `ics55_dev` binary command if source changes could affect runtime behavior.
- [x] Update task artifacts with final findings and remaining exceptions.

## Final Validation Results

Final source check passed:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result:

- `format`: 0 in-scope findings.
- `tidy`: 0 in-scope findings.
- `headers`: 0 in-scope findings.
- `cmake`: 0 in-scope findings.
- `iwyu`: 0 in-scope findings.

Runtime validation passed:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Result:

- `iCTS run successfully.`
- CTS main flow finished with total elapsed time `16.154 s`.
- Report stage finished; DEF, Verilog, statistics, metrics, SVG, and GDS reports were generated.

## Validation Commands

Focused build examples:

```bash
ninja -C build icts_source_database_adapter_fast_sta
ninja -C build icts_source_database_io
ninja -C build icts_source_module_characterization
ninja -C build icts_source_module_routing_bst
```

Final source check:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Runtime validation command when needed:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

## Risk Points

- FastSTA public helper types are currently used by optimization and characterization. The correction must avoid broad mechanical moves that create
  worse cross-module coupling.
- `CharacterizationBufferCell` is already used outside `module/characterization`; decide whether it is a public characterization contract before
  hiding it.
- `database/io` has user preference constraints that conflict with previously introduced names. Prefer local writer implementation over inventing a
  new root helper name.
- `database/design` and similar data-model roots should not be forced into behavior-directory rules without user confirmation.
- New-file naming cleanup must wait until file moves settle, otherwise the rename list will mix true new files with moved files and become noisy.
