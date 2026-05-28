# merge main into cts_refactor

## Goal

Analyze the impact of merging `main` into the current `cts_refactor` branch before performing the merge.

The immediate user value is a concrete pre-merge report that identifies:

- the previous `main` -> `cts_refactor` merge point;
- changes introduced on `main` since that point;
- whether merging the current `main` state into `cts_refactor` is expected to conflict.

## Requirements

- Treat the current branch as `cts_refactor`.
- Refresh remote refs before analysis so `origin/main` is current.
- Identify the last merge commit where `main` was merged into `cts_refactor`, using git history evidence rather than assumptions.
- Summarize commits and touched files on `main` after that merge point.
- Run a non-committing conflict precheck and report any conflicted paths if present.
- Do not perform or commit the actual merge unless the user explicitly asks for it later.
- For the actual `main -> cts_refactor` merge, preserve current-branch CTS behavior and architecture by default.
- Explicitly identify CTS-related changes that arrived from `origin/main` after the last merge point.
- For non-CTS changes from `origin/main`, prefer integrating mainline changes unless they break CTS-facing contracts or validated CTS behavior.

## Acceptance Criteria

- [x] Task directory exists and records the requested analysis scope.
- [x] Report names the last merge point and the exact ref analyzed for `main`.
- [x] Report summarizes post-merge changes on `main`.
- [x] Report states whether a merge conflict is expected and lists conflicting files if any.
- [x] Working tree is left without an in-progress merge.

## Notes

- Lightweight PRD-only task. This turn is analysis only, not implementation of the merge.
- The task has grown into a complex merge-planning task. `design.md` and `implement.md` capture the merge strategy before activation.
