# merge latest main to cts_refactor

## Goal

Bring the latest `main` changes into `cts_refactor` so the later `main <- cts_refactor`
merge is easier to review, while preserving CTS refactor behavior and proving the
`ics55_dev` iCTS script produces the same result before and after the merge.

## Background / Known Context

- Current working branch before task start is `cts_refactor` at `893b67361`.
- Local `main` is still at `57d0e8b31`, while `origin/main` is at `9f75fcc51`.
- The merge base between `cts_refactor` and `origin/main` is `57d0e8b31`.
- A preview merge from `origin/main` into `cts_refactor` found 9 conflict paths.
- The task must leave merge changes uncommitted for user review.
- The validation command is:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

## Requirements

- Update local `main` to the latest `origin/main` before merging.
- Merge latest `main` into `cts_refactor`.
- Resolve CTS and CTS-related external interface conflicts in favor of `cts_refactor`
  semantics.
- Review iSTA/iPA conflicts for correct semantics rather than blindly taking either
  side.
- Other module changes may follow latest `main` after checking they do not affect CTS.
- Keep `src/apps/CMakeLists.txt` unchanged from the current `cts_refactor` state.
- Do not create a merge commit or any other commit in this task.
- Report merge status, conflict decisions, diff summary, and validation result to the user.

## Known Conflict Areas

Previewed with a detached worktree merge of `origin/main` into `cts_refactor`:

- `.gitignore` - both sides changed ignore rules. Must preserve CTS/Trellis artifact
  ignores and bring in relevant latest main build/release ignores.
- `AGENTS.md` - add/add conflict. Must keep Trellis project instructions valid.
- `src/interface/python/py_ista/py_ista.cpp` - iSTA Python interface conflict. Requires
  semantic review.
- `src/interface/python/py_ista/py_ista.h` - iSTA Python interface conflict. Requires
  semantic review.
- `src/interface/python/py_ista/py_register_ista.h` - iSTA Python registration conflict.
  Requires semantic review.
- `src/operation/iCTS/README.md` - CTS documentation conflict. Prefer refactor branch
  CTS content unless main has neutral documentation updates worth preserving.
- `src/operation/iCTS/source/database/design/Net.hh` - CTS source conflict. Prefer
  refactor branch semantics.
- `src/operation/iCTS/source/utils/synthesis_operator/SolverNetBuilder.cc` - deleted by
  `cts_refactor`, modified by main. Keep refactor deletion unless investigation proves
  the file is still used.
- `src/operation/iCTS/source/utils/synthesis_operator/TopologyBuilderOperator.cc` -
  deleted by `cts_refactor`, modified by main. Keep refactor deletion unless
  investigation proves the file is still used.

## Acceptance Criteria

- [ ] Local `main` points at the latest fetched `origin/main`.
- [ ] `cts_refactor` contains a no-commit merge of latest `main` with no unresolved
  conflict markers and no unmerged index entries.
- [ ] CTS code and CTS-facing interfaces retain `cts_refactor` behavior.
- [ ] iSTA/iPA differences are summarized with the chosen semantic resolution.
- [ ] Other latest main changes are summarized, including whether they affect CTS.
- [ ] `src/apps/CMakeLists.txt` remains unchanged from the pre-merge `cts_refactor`
  version.
- [ ] The `ics55_dev` iCTS validation command runs before and after the merge, and the
  meaningful output/result comparison shows no behavior difference.
- [ ] No commit is created; changes remain available for user review.

## Out of Scope

- Pushing branches.
- Creating the final `main <- cts_refactor` merge.
- Refactoring CTS beyond what is necessary to resolve this merge.
- Updating project specs unless the merge exposes a durable rule that should be captured.
