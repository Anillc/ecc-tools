# Merge main into cts_refactor

## Goal

Merge `main` into `cts_refactor` as the first half of the later two-way integration plan. This task only performs `main -> cts_refactor`; it must leave the merge result uncommitted until the user explicitly approves a commit, and it must verify that the iCTS binary/runtime behavior remains unchanged from the pre-merge `cts_refactor` baseline.

## Background / Known Context

- Current branch is `cts_refactor`; the intended target is to bring in the local `main` branch, not to merge `cts_refactor` back to `main`.
- After fetching, local `main` is `57d0e8b31663`. `origin/main` has moved to `9f75fcc51ec6`, but attempting to merge that remote ref produced 271 conflicts from newer iRCX/Trellis/platform work and was aborted as out of scope for this task.
- Existing completed Trellis tasks have been moved to archive before this task starts:
  - `05-12-h-tree-performance-optimization`
  - `05-13-ics55-huge-post-pl-icts-run`
- A prior attempted `main -> cts_refactor` merge changed the iCTS result. That result difference was traced to liberty leakage power unit conversion being overwritten by `main` behavior.
- The prior bad merge is preserved only as a backup reference, not as the current branch state:
  - `backup/cts-refactor-post-main-merge-20260513`
- Current `cts_refactor`/remote CTS state includes the post-PL huge iCTS fix at `7767c90df fix: run huge post-pl iCTS case`.

## Known Conflict / Semantic Issues

- CTS and CTS-related external interfaces must follow `cts_refactor` semantics by default.
- Liberty parsing requires special protection against incorrect behavior overwrite:
  - `cts_refactor` handles `leakage_power_unit`, stores a milliwatt scale, and converts `cell_leakage_power` and leakage `value` through that scale.
  - The previous post-merge backup dropped the conversion path and left leakage power raw.
  - For the ICS55 liberty where `leakage_power_unit : "1nW"`, dropping conversion changes root-driver power from uW/mW-scale values to W-scale values and changed root-driver selection.
  - This issue affects CTS root-driver characterization via STA/liberty data and must not regress.
- `src/apps/CMakeLists.txt` must keep the current `ics55_dev` output/configuration behavior; do not accept a `main` change that points it back to `sky130_gcd`.
- iSTA and iPA diffs must be reviewed semantically, not blindly overwritten. Their timing, power, liberty, and adapter-facing behavior can affect CTS.
- `scripts/design` CTS config files should be normalized using `scripts/design/ics55_dev/iEDA_config/cts_default_config.json` as the style reference:
  - remove obsolete/redundant old CTS keys,
  - preserve values for keys still in use,
  - add new keys used by `ics55_dev` where applicable.
- Other module diffs may generally follow `main` after review, but the merge report must note whether they affect CTS behavior or CTS-facing contracts.

## Requirements

- Merge local `main` into `cts_refactor` with `--no-commit`.
- Resolve conflicts according to these precedence rules:
  - CTS implementation and CTS external interfaces: prefer `cts_refactor`.
  - iSTA/iPA/liberty/timing/power behavior: inspect conflict semantics and preserve the behavior required for correct CTS results.
  - Other modules: accept `main` when review shows no CTS impact.
  - Outer/app CMake: keep current `ics55_dev` behavior.
- Keep all Trellis task creation/archive changes uncommitted as normal worktree state unless the user later approves a commit.
- Run a pre-merge iCTS baseline before merging and save logs/artifacts under this task.
- After resolving the merge, build clean enough to produce the runnable binary and run:
  - `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- Compare post-merge runtime output against the pre-merge baseline and report whether important CTS results differ.
- Summarize:
  - conflicts encountered and resolution choice,
  - iSTA/iPA/liberty semantic decisions,
  - direct CTS impact from other modules,
  - diff summary,
  - remaining risks or notes for the later `main <- cts_refactor` merge.
- Do not commit or push.

## Acceptance Criteria

- [ ] Existing completed Trellis tasks are archived and no unrelated active task remains.
- [ ] This task has `prd.md`, `design.md`, and `implement.md` before execution starts.
- [ ] local `main` is merged into `cts_refactor` with conflicts resolved and no merge commit created.
- [ ] CTS/liberty behavior preserves leakage power unit conversion and root-driver semantics from `cts_refactor`.
- [ ] `src/apps/CMakeLists.txt` retains `scripts/design/ics55_dev`.
- [ ] iSTA/iPA diffs are reviewed and summarized.
- [ ] `scripts/design` CTS configs are cleaned/synchronized per the `ics55_dev` reference style.
- [ ] Pre-merge and post-merge iCTS command logs are saved under this task.
- [ ] Post-merge iCTS result is checked against the pre-merge baseline, with any differences explained before asking for commit approval.
- [ ] Final report includes a later `main merge cts_refactor` checklist.

## Out of Scope

- Merging `cts_refactor` into `main`.
- Committing the merge result without explicit user approval.
- Unrelated refactors not required to resolve the merge or preserve CTS behavior.

## Notes

- This task intentionally records the previously identified liberty overwrite problem so it is treated as a known regression risk during conflict resolution rather than rediscovered after runtime validation.
