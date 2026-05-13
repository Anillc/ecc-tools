# Implementation Checklist

- [x] Confirm current branch, remote sync state, and active Trellis task state.
- [x] Save pre-merge git metadata and baseline iCTS runtime log under `artifacts/pre_merge/`.
- [x] Run a clean/all build as far as the local environment permits; record any blocker logs.
- [ ] Merge local `main` into `cts_refactor` with `--no-commit --no-ff`.
- [ ] Resolve conflicts with the task precedence rules:
  - CTS and CTS-facing APIs follow `cts_refactor`.
  - iSTA/iPA/liberty semantics are reviewed manually.
  - `src/apps/CMakeLists.txt` keeps `ics55_dev`.
  - Other modules follow `main` after CTS-impact review.
- [ ] Clean old CTS config keys in `scripts/design` while preserving active values and adding the `ics55_dev` CTS config keys where needed.
- [x] Rebuild after conflict resolution.
- [x] Run post-merge iCTS and save logs under `artifacts/post_merge/`.
- [x] Compare pre/post logs and generated outputs; summarize identical metrics or explain differences.
- [ ] Produce final merge report and later `main <- cts_refactor` checklist.

## Validation Commands

```bash
git status --short --branch
git merge --no-commit --no-ff main
bash build.sh -d -y
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

If `bash build.sh -d -y` cannot complete in the local environment, record the exact failing command/log and continue only if an existing or partial binary can still run the requested iCTS script.

## Review Gates

- Do not commit after the merge.
- Do not push.
- Report unresolved semantic risks before asking for commit approval.
- If post-merge iCTS output differs from pre-merge, stop and explain the root cause instead of committing.
