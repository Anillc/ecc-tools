# Worktree + Claude Code Agent Pitfall

> Captured 2026-05-19 from Session 62. **Do not use `isolation: "worktree"` with custom `trellis-*` agents** until a workaround is in place.

## Symptom

Dispatched 4 `trellis-implement` agents in parallel with `isolation: "worktree"` for child tasks T3 / T4 / T6 / T7. All 4 ended in inconsistent partial states:
- T3 ran 117 minutes then died with socket error; only `types/` + `dmp_ceff/` skeleton directories created, old 28 fast_sta files untouched
- T4 created 3 subdir skeletons + 2 facade headers; new `.cc` files referenced headers (`BstParameters.hh`, `BstRouter.hh`, `types/FastStaEnums.hh`) that the agent had not yet created → editor reported `file not found`
- T6 deleted old `FastClustering*Polish.cc` / `Boundary*.cc` / `Partition.cc` / ... files, created `detail/` subdir; never reached build verification
- T7 deleted `STAAdapterInternal.{hh,cc}` + `Wrapper.cc`, modified 14 STAAdapter cc files, created `IdbBridge.{hh,cc}` + `StaAdapterRuntime.{hh,cc}` + `TreeLoadTypes.hh`; never reached build verification

## Root Cause

`git worktree add` only checks out git-tracked files. The `.claude/` directory at the project root is gitignored (custom agents at `.claude/agents/*.md`, `.claude/memory/`, `.claude/settings.local.json`, `.claude/hooks/`, `.claude/skills/`, `.claude/commands/` — none are tracked). When Claude Code dispatches an agent into a new worktree:

- the worktree contains the source tree and `.trellis/` (both tracked)
- **the worktree does NOT contain `.claude/`** — so the agent has no access to the custom `trellis-implement` definition, no project-specific skills, no hooks, no memory
- the agent falls back to whatever generic capabilities the Claude Code harness gives it without the project's customizations
- this triggers a class of subtle failures: missing tools, missing hooks for context injection, no `trellis-before-dev` pre-flight, no `trellis-check` follow-up, and degraded session resilience that may have contributed to the 117-min socket failure

## Secondary Issue: Worktree HEAD Divergence

`git worktree add` was issued from `cts_refactor` HEAD `d010807be`, but 3 of 4 worktrees ended up at HEAD `111b9f080` (likely because the worktree's branch tracked the upstream `origin/cts_refactor` which was 1 commit ahead). Merging back would have introduced 13+ unrelated commits (`fix(wheel)`, PR #27 merges, etc.) on top of the agent's intended edits — making it nearly impossible to extract just the refactor work.

## Recommendation

**Until the harness or Trellis tooling solves this, do not pass `isolation: "worktree"` to `Agent` calls when:**
1. the agent type is a custom `.claude/agents/*.md` definition (e.g. `trellis-implement`, `trellis-check`, `trellis-research`)
2. the work requires `.trellis/` workflow hooks / context injection
3. the work needs the project's custom skills (`trellis-before-dev`, `trellis-check`, `trellis-update-spec`, etc.)

**Acceptable alternatives:**
- **Main tree, serial dispatch**: dispatch one `trellis-implement` agent at a time without `isolation`; lock scope per dispatch to ≤ 60 minutes of work; ensure each dispatch ends in a verifiable build state. This is what Session 63 will use.
- **Manual implementation**: for low-risk, mechanical work, do the edits in the main session without a sub-agent.
- **agent-team**: untested in this project; would still need to solve the `.claude/` propagation problem if any team member is in a worktree.

**A possible future fix** (not validated):
- Copy `.claude/` from project root into the new worktree before launching the agent, then remove on cleanup. This is a `pre-spawn` hook that the harness does not currently provide.
- Or: track a subset of `.claude/` (just `agents/` and `hooks/`) in git via a careful `.gitignore` exception. This has implications for any developer who customizes locally.

## Recovery Procedure (used at the end of Session 62)

```bash
# 1. Stop background agents
# (use TaskStop in Claude Code for each running agent id)

# 2. Force-remove worktrees (they are locked while the agent process was alive)
git worktree remove -f -f .claude/worktrees/agent-<id>

# 3. Remove the leftover .claude/worktrees/ directory itself
rm -rf .claude/worktrees

# 4. Delete the agent branches
git branch -D worktree-agent-<id>

# 5. Verify clean
git worktree list  # should show only the main tree
git branch         # should show only your normal branches
git status         # main tree should be unaffected
```
