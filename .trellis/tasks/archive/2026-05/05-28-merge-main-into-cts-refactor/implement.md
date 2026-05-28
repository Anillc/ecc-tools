# Merge Implementation Plan

## Pre-Merge

- [x] Confirm the user approves starting the actual merge after reviewing this plan.
- [x] Capture current `cts_refactor` HEAD and `git status`.
- [x] Run and save the pre-merge iCTS reference validation output.
- [x] Create a backup branch from `cts_refactor` and merge in the current worktree.
- [x] Confirm `origin/main` is freshly fetched.

## Merge

- [x] Run `git merge --no-commit --no-ff origin/main` on the current worktree.
- [x] Save the initial conflict list and status summary.
- [x] Resolve iCTS architecture conflicts with current `cts_refactor` as source of truth.
- [x] Review and selectively port main-side CTS fixes:
  - [x] `STAAdapter.cc` from `f3b719ae1`
  - [x] `Wrapper.cc`/clock-writer safe iDB API usage from `11f2a571c`
  - [x] `source/database/io/CMakeLists.txt` from `77149cd71` / `a08e56206` reviewed; net effect intentionally not kept
  - [x] CTS script/config layout changes from `344d4fd21` through `fd31a6c16` reviewed; old main-side iCTS additions removed
- [x] Resolve CTS-facing Tcl/Python/default-config/tool-manager/app files by preserving
  current CTS behavior.
- [x] Resolve shared iSTA/iPA/liberty/database overlaps by semantic review.
- [x] Accept non-CTS mainline changes where they do not affect CTS contracts.

## Verification

- [x] Check for unmerged entries.
- [x] Search for conflict markers.
- [x] Review final diff by area:
  - [x] iCTS
  - [x] CTS-facing interfaces/scripts
  - [x] shared iSTA/iPA/database/liberty
  - [x] external non-CTS mainline changes
  - [x] Trellis/agent/local metadata
- [x] Build.
- [x] Run post-merge iCTS validation.
- [x] Compare pre/post CTS metrics and stable generated outputs.

## Completion

- [x] Summarize accepted main-side CTS changes and rejected/ignored CTS changes.
- [x] Summarize external non-CTS changes accepted from main.
- [x] Ask for approval before committing the merge result.
- [ ] If approved, commit the merge result on `cts_refactor`.
