# fix all target compile failures

## Goal

Restore a successful CMake `all` target build on the current `cts_refactor` branch after recent changes.

The task exists because recent iCTS and build-system changes may have left targets outside the narrower iCTS validation set unable to compile or link.

## Requirements

- Reproduce the current `all` target failure from the existing build tree, or from a regenerated build tree if the existing one is stale.
- Identify compile or link failures caused by recent repository changes.
- Fix source/CMake issues needed for `all` to compile successfully.
- Keep fixes scoped to build correctness; avoid unrelated refactors, feature behavior changes, and cosmetic cleanup.
- Preserve existing generated/user changes unless they are directly required to repair the build.
- If failures come from optional or environment-specific targets, document the evidence and the chosen handling.

## Acceptance Criteria

- [ ] `cmake --build build --target all -j 8` completes successfully on the current branch.
- [ ] Any changed code is covered by the smallest relevant build/test command available for the touched area.
- [ ] No unrelated task/archive/journal changes are included beyond this task's artifacts.
- [ ] Remaining non-build issues, if any, are explicitly documented as out of scope.

## Notes

- Current branch at task creation: `cts_refactor`.
- Current working tree at task creation: clean except this task directory.
- Initial suspected risk area: recent CTS/iCTS changes, but the investigation must use actual `all` build output rather than assuming the failure source.

## Out of Scope

- Reworking the iCTS/iSTA/iPA unbind architecture unless the build failure directly requires it.
- Changing default build options or disabling targets to hide real compile errors.
- Fixing runtime regressions or test expectation changes that are not needed for `all` compilation.
