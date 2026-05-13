# design.md

## Technical Design

This task is a no-commit integration of latest `main` into `cts_refactor`.

The local `main` branch will be fast-forwarded to the fetched `origin/main` commit before
the merge. The merge into `cts_refactor` will use `--no-commit` so the final state remains
reviewable and uncommitted.

Conflict handling follows these boundaries:

- CTS source, CTS documentation, CTS CMake, Tcl/Python CTS adapters, and tool-manager CTS
  APIs follow `cts_refactor` unless a latest main change is demonstrably independent and
  compatible.
- iSTA and iPA conflicts are reviewed by intent. The chosen version must preserve current
  CTS call-site needs while integrating latest main API additions where they are compatible.
- Non-CTS modules can follow latest main after a quick impact check against CTS entry
  points and build wiring.
- `src/apps/CMakeLists.txt` is treated as pinned to the current `cts_refactor` version.

Validation compares the `ics55_dev` iCTS run before and after merge. Runtime-only or
timestamp-only output should be excluded from the behavior comparison; metrics, generated
netlist structure, and stable report content are the meaningful outputs.

## Rollout / Rollback

No commit will be created in this task. Rollback is therefore local:

- Before merge: record the `cts_refactor` HEAD and capture pre-merge validation artifacts.
- During merge: if resolution goes wrong, abort the merge if possible or reset back to the
  recorded pre-merge HEAD.
- After merge: leave changes uncommitted for user review.
