# implement.md

## Implementation Checklist

- [x] Record pre-merge `cts_refactor` HEAD and working-tree status.
- [x] Update local `main` to latest `origin/main` without modifying `cts_refactor` files.
- [x] Run the pre-merge `ics55_dev` iCTS validation command and save stable outputs under
  the task artifacts directory.
- [x] Merge latest `main` into `cts_refactor` with `--no-commit`.
- [x] Resolve `.gitignore` and `AGENTS.md` conflicts by preserving current project-local
  Trellis behavior and compatible latest main updates.
- [x] Resolve CTS conflicts in favor of `cts_refactor` semantics.
- [x] Review iSTA/iPA changes and resolve by correct API/semantic behavior.
- [x] Review non-CTS module changes for CTS impact, then keep latest main changes where
  safe.
- [x] Confirm `src/apps/CMakeLists.txt` matches the pre-merge `cts_refactor` version.
- [x] Check for unmerged entries and conflict markers.
- [x] Run the post-merge `ics55_dev` iCTS validation command and compare stable outputs
  against pre-merge artifacts.
- [x] Summarize merge decisions, diff, validation, and remaining review risks.

## Validation

```bash
git status --short --branch --untracked-files=no
git ls-files -u
rg -n "<<<<<<<|=======|>>>>>>>" --glob '!build/**'
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

## Review Gates

- Start gate: planning artifacts exist and record the known conflict areas.
- Finish gate: no unresolved conflict entries, no conflict markers, validation comparison
  has no meaningful pre/post differences, and changes are left uncommitted.
