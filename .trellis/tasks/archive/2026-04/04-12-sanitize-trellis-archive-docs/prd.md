# Sanitize Trellis Archive Docs

## Goal
Sanitize current-tree `.trellis` archive documents so they no longer contain explicit third-party repository names, repository-local absolute paths, or external implementation-specific code details.

## Requirements
- Remove explicit third-party project-name references from current-tree `.trellis` archive documents.
- Replace repository-local absolute paths with neutral descriptions.
- Generalize external implementation flow and API details so the documents preserve intent without exposing third-party code specifics.
- Keep the documents internally consistent after sanitization.
- Validate a branch-local history rewrite workflow that preserves commit dates while removing the targeted references from future clones.
- Remap recorded workspace commit hashes after the rewritten history is generated.

## Acceptance Criteria
- [x] Current-tree `.trellis` files no longer contain the disallowed third-party project name.
- [x] Current-tree `.trellis` files no longer contain the repository-local absolute path to that external source tree.
- [x] Archived task docs preserve useful implementation context in neutral wording.
- [x] A local `git filter-repo` dry run preserved commit dates while removing the targeted references from `cts_refactor`.
- [x] Workspace commit references were remapped to the rewritten history.
- [x] A fresh clone verification passed after the sanitized `cts_refactor` rollout.

## Technical Notes
- Primary scope is `.trellis/tasks/archive/2026-02/02-27-cts-characterization/02-27-cts-characterization/`.
- `.trellis/workspace/dawnli/index.md` and `journal-1.md` were updated after the rewrite to replace old commit references with the rewritten hashes.
- Rewrite preparation and safety backups were created before rollout, and a fresh clone check was used as the final verification step.
