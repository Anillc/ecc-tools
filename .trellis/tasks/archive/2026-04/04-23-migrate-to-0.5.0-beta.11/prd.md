# Migrate to v0.5.0-beta.11

## Goal

Complete the Trellis migration from `0.4.x` to `0.5.0-beta.11` on top of the existing uncommitted migration changes in this repository, without discarding or overwriting local customizations.

## What I Already Know

* The task already exists at `.trellis/tasks/04-23-migrate-to-0.5.0-beta.11/`.
* The repository already contains substantial migration-related edits under `.trellis/`, `.codex/`, `.agents/`, and `.agent/`.
* New Codex agent files already exist at `.codex/agents/trellis-{implement,check,research}.toml`.
* New Trellis skill directories already exist at `.agents/skills/trellis-*`.
* Old Trellis/Codex files are currently shown as deleted in git status, which is consistent with a rename/remove migration.
* The task context files `implement.jsonl` and `check.jsonl` have not been created yet.
* `.codex/config.toml` already documents that `features.codex_hooks = true` must be enabled in the user-level `~/.codex/config.toml`.
* Direct repo grep did not find live code references to bare `subagent_type="implement"|"check"|"research"`; the remaining old command references are in files already marked deleted under `.agent/workflows/`.
* The user confirmed the current uncommitted migration changes should be preserved and used as the basis for completing the migration.

## Assumptions

* The current uncommitted changes are intentional migration work, not accidental edits to revert.
* This project should remain on the Codex platform after migration.
* Removed features such as the old multi-agent pipeline should stay removed unless a live file still requires a documented replacement.

## Open Questions

* None at the moment. Proceed unless a conflicting local customization is discovered during `trellis update --migrate`.

## Requirements

* Keep all existing local migration edits unless a concrete conflict requires a targeted merge.
* Make the migration task the active Trellis task for this session.
* Convert this task from planning-only to execution-ready by initializing Codex task context files.
* Run the required migration command, `trellis update --migrate`, and preserve local customizations when prompts appear.
* Verify the migration result with a second `trellis update` run and resolve any remaining outdated files or references if needed.
* Ensure active task context files point to the new `trellis-*` skill/agent paths.
* Check live Trellis/Codex project files for leftover references to retired commands or old agent names and update them if they still participate in the active workflow.
* Preserve the project-level reminder that Codex hooks require `features.codex_hooks = true` in the user config.

## Acceptance Criteria

* [ ] `.trellis/.current-task` points to `.trellis/tasks/04-23-migrate-to-0.5.0-beta.11`.
* [ ] Task context files exist for this task and are initialized for Codex.
* [ ] `trellis update --migrate` completes without discarding local customizations.
* [ ] A follow-up `trellis update` reports no remaining required migration work, or any residual diffs are explained and intentionally kept.
* [ ] Live Codex/Trellis workflow files use the `trellis-*` agent and skill naming expected by `0.5.0-beta.11`.
* [ ] No active task context file for this task points at retired skill paths.

## Definition of Done

* Migration command completed and verified
* Task context initialized for Codex
* Relevant validation/check commands run
* Remaining risks or manual follow-ups documented

## Out of Scope

* Reverting unrelated user changes outside the migration scope
* Restoring removed 0.4-era features such as the old multi-agent pipeline
* Migrating historical archived tasks unless they directly block the active workflow

## Technical Notes

* Current migration-related git status already shows:
  old `.codex/agents/{implement,check,research}.toml` deleted, new `trellis-*` versions added.
* Current migration-related git status already shows:
  old `.agents/skills/*` entries deleted, new `.agents/skills/trellis-*` entries added.
* Current task was switched to `.trellis/tasks/04-23-migrate-to-0.5.0-beta.11` during this session.
* Before initialization, this task had no `implement.jsonl` or `check.jsonl`.
* The main remaining work is to finish migration execution, verify the repo state, and regenerate task context under the new naming.
