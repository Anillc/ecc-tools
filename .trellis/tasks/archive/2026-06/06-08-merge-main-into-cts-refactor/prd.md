# Update main and merge into cts_refactor

## Goal

Update the local `main` branch to the latest remote `origin/main`, then attempt to merge the latest `main` into the current
`cts_refactor` branch and report whether conflicts occur.

## Requirements

- Start from a clean working tree on `cts_refactor`.
- Fetch latest remote branch state before updating local `main`.
- Update local `main` to match the latest remote `origin/main` when it is safe to do so.
- Attempt the merge into `cts_refactor`.
- If conflicts occur, leave the repository in the conflict state and report conflicted files.
- If no conflicts occur, complete the merge and report the resulting commits.
- Do not alter unrelated files or revert existing user work.

## Acceptance Criteria

- [ ] A Trellis task exists and is active for the merge check.
- [ ] Local `main` has been updated from `origin/main`.
- [ ] `main` has been merged or attempted to merge into `cts_refactor`.
- [ ] Conflict status is explicitly reported.
- [ ] If a merge commit is created, its hash is reported.
