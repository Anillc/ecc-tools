# Update Backend Spec from Codebase

## Goal
Restore and update the backend spec files (.trellis/spec/backend/) that were previously filled with iCTS-specific content but got reverted to empty templates.

## Requirements
- Recover original spec content from git diff (the `-` lines show what was there before)
- Verify accuracy against current codebase
- Add any new patterns/classes discovered in the current code
- Maintain consistency with existing project-constraints.md

## Acceptance Criteria
- [ ] database-guidelines.md filled with current data model, singleton patterns, memory management
- [ ] directory-structure.md filled with current three-tier architecture and module layout
- [ ] error-handling.md filled with current error handling patterns
- [ ] logging-guidelines.md filled with current CTS_LOG_* usage patterns
- [ ] quality-guidelines.md filled with current coding standards
- [ ] index.md updated to reflect filled status

## Technical Notes
- The git diff shows the old content (lines prefixed with `-`)
- project-constraints.md already contains naming conventions and clang-format/clang-tidy rules
- Avoid duplicating content already in project-constraints.md; reference it instead
