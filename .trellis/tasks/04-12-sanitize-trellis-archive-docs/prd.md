# Sanitize Trellis Archive Docs

## Goal
Sanitize current-tree `.trellis` archive documents so they no longer contain explicit third-party repository names, repository-local absolute paths, or external implementation-specific code details.

## Requirements
- Remove explicit third-party project-name references from current-tree `.trellis` archive documents.
- Replace repository-local absolute paths with neutral descriptions.
- Generalize external implementation flow and API details so the documents preserve intent without exposing third-party code specifics.
- Keep the documents internally consistent after sanitization.
- Do not rewrite Git history in this phase.

## Acceptance Criteria
- [ ] Current-tree `.trellis` files no longer contain the disallowed third-party project name.
- [ ] Current-tree `.trellis` files no longer contain the repository-local absolute path to that external source tree.
- [ ] Archived task docs preserve useful implementation context in neutral wording.
- [ ] No Git history rewrite is performed in this phase.

## Technical Notes
- Primary scope is `.trellis/tasks/archive/2026-02/02-27-cts-characterization/02-27-cts-characterization/`.
- `.trellis/workspace/dawnli/index.md` and `journal-1.md` are out of scope for this phase unless they contain current-tree sensitive text.
