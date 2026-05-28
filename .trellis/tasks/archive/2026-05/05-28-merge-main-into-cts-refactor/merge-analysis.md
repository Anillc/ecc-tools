# Merge Analysis: origin/main into cts_refactor

Date: 2026-05-28

## Refs

- Current branch: `cts_refactor`
- Current HEAD: `3e639d49e` (`chore: record journal`)
- Analyzed main ref: `origin/main` at `493c1ea77` (`Merge pull request #49 from openecos-projects/rcx_main`)
- Local `main`: `0c49265bf`, behind `origin/main` by 90 commits

## Last main Merge Into cts_refactor

The latest explicit merge of `main` into `cts_refactor` is:

- Merge commit: `364215d55` (`merge: bring latest main into cts_refactor`)
- Date: `2026-05-13 14:41:16 +0800`
- Parents:
  - `893b67361d53eda8c8f1cba016dfad3194c37275` (`cts_refactor` side)
  - `9f75fcc51ec6618efbcfb72a338c892a8db18b0b` (`main` side)

`git merge-base HEAD origin/main` is also `9f75fcc51ec6618efbcfb72a338c892a8db18b0b`, confirming `cts_refactor` has not incorporated later `origin/main` commits.

## Changes Since That Merge Point

From `9f75fcc51ec6618efbcfb72a338c892a8db18b0b` to `origin/main`:

- Commit count: 91
- File delta: 1661 files changed, 140610 insertions, 317236 deletions
- Major areas by touched files:
  - `src/operation`: 806 files
  - `src/interface`: 266 files
  - `scripts/design`: 265 files
  - `src/third_party`: 190 files
  - `src/database`: 98 files
  - `src/platform`: 14 files

First-parent mainline changes include:

- `0c49265bf` milestone: land CTS refactor integration baseline
- DB and infra refactors: PRs #37, #39, #43, #44, #45
- iRCX refactors and SPEF/reporting/temperature support: PRs #30, #33, #35, #38, #41, #42, #49
- iDRC updates and DB adaptation: PRs #46, #48 plus direct iDRC commits
- build/wheel/rust/linker updates: PRs #26, #28, #32
- script/workspace/design reorganization for `ics55`, `sky130`, and old `*_gcd` layouts
- third-party changes including adding `gdstk` and removing `tcl_qt`

For comparison, from the same merge point to `cts_refactor`:

- Commit count: 3481
- File delta: 2595 files changed, 2706538 insertions, 23164 deletions
- The branch has heavy iCTS/Trellis/third-party HiGHS work after the previous main merge.

## Conflict Precheck

Command shape used:

- `git fetch origin --prune`
- Temporary detached worktree from current `HEAD`
- `git merge --no-commit --no-ff origin/main`
- `git merge --abort`
- temporary worktree removed

Result:

- Merge status: conflict
- Unmerged paths: 307
- iCTS conflicts: 297
- non-iCTS conflicts: 10

Conflict status distribution:

- `AA`: 259
- `DD`: 15
- `UU`: 13
- `AU`: 11
- `UD`: 5
- `UA`: 3
- `DU`: 1

Primary conflict buckets:

- `src/operation/iCTS/source`: 219 paths
- `src/operation/iCTS/test`: 75 paths
- `src/operation/iCTS/api`: 2 paths
- `scripts/design/ihp130_gcd`: 2 paths
- single-path conflicts also appear in `.gitignore`, `AGENTS.md`, `src/apps`, `src/database`, `src/operation`, `src/third_party`, `scripts/design/ics55_gcd`, and `scripts/design/sky130_gcd`

Representative conflict causes:

- Directory rename/rename conflicts where the same old iCTS directories were reorganized differently:
  - `src/operation/iCTS/source/module/synthesis`
  - `src/operation/iCTS/source/module/router`
  - `src/operation/iCTS/source/module/flow`
  - `src/operation/iCTS/source/data_manager/io/report`
- File rename/rename conflicts, for example:
  - `CtsClock.hh` renamed to different destinations on each side
  - `Net.cc` renamed to different destinations on each side
  - `CtsReport.hh` renamed to different destinations on each side
  - `Solver.hh` renamed to different destinations on each side
- Large add/add conflicts across iCTS test and source modules.
- Non-iCTS content/delete conflicts include `.gitignore`, `AGENTS.md`, `src/third_party/CMakeLists.txt`, and `src/operation/iSTA/source/module/sta/Sta.cc`.

## Conclusion

Merging current `origin/main` into `cts_refactor` will conflict heavily. The conflict is not a small textual merge; it is primarily an architecture divergence between the current iCTS refactor branch and the iCTS baseline/reorganization that later landed on `origin/main`.

A direct merge is possible, but it should be planned as a real integration task. The first decision should be which iCTS directory architecture is authoritative, then resolve the iCTS source/test tree around that decision before chasing individual add/add conflicts.
