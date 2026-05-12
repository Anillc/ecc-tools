# small-fanout H-tree legality root-cause debugging implementation plan

## Implementation Checklist

- [x] Create dedicated Trellis task linked under `05-12-h-tree-performance-optimization`.
- [x] Record initial reproduction context from the dev `ics55_dev` flow.
- [x] Identify primary failing stage from current logs.
- [x] Preserve current failing config and log excerpts in `research/fanout4-initial-evidence.md`.
- [x] Reference a baseline passing comparison for `max_fanout = 32` from the parent sweep artifacts.
- [x] Extract equivalent sink-load-region evidence through existing logs and code inspection; no noisy permanent debug logging was added.
- [x] Re-run `max_fanout = 4` after the fix and capture CTS internal success from `result/cts/cts.log`.
- [x] Trace the leaf fanout-relative algorithm inputs and compare clustered local-buffer loads against original sink distribution.
- [x] Inspect H-tree topology generation, segment frontier construction, and topology pattern assembly to document where intermediate-level fanout is checked, ignored, or only implied.
- [x] Resolve high-risk hypotheses with targeted evidence in `research/root-cause-report.md`:
  - fanout threshold sweep, e.g. `4, 8, 16, 32`.
  - depth exploration window change.
  - sink clustering toggle.
  - leaf fanout-relative grouping with clustered loads versus original sink/load-group accounting, if code-local instrumentation is needed.
  - intermediate-level fanout reporting or forced rejection, if code-local instrumentation is needed.
  - monotone pruning disabled or reported-only, if code-local instrumentation is needed.
- [x] Write `research/root-cause-report.md` with confirmed/rejected hypotheses and recommended fix path.
- [x] Document why flow-level validation is the practical regression gate for this design-level legality bug.
- [x] Do not use broad `ecc dev` checks as a debugging gate; rely on targeted inspection, focused tests where needed, and the final `ics55_dev` flow.
- [x] Validate final behavior with the exact `ics55_dev` command and CTS internal status, not process exit code only.
- [x] Confirm the final root-cause report explicitly resolves the clustering/leaf fanout-relative question and the intermediate-level fanout question.
- [x] Temporarily restore `max_fanout = 32`, rerun the dev binary flow, and confirm the original iter3/step10 baseline is not regressed.
- [x] Restore the dev config back to `max_fanout = 4` after the temporary baseline run.
- [x] Run final `ecc_dev_tools` iCTS check after the requested baseline regression.

## Validation Commands

Primary reproduction:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
/usr/bin/time -f 'WALL_TIME_SECONDS=%e\nMAX_RSS_KB=%M' \
  -o ./result/fanout4_debug_run_time.txt \
  ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl \
  > ./result/fanout4_debug_run.log 2>&1
```

Final acceptance:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Internal status checks:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
rg -n 'max_fanout|no_legal_depth_candidates|H-tree build failed|failed_clocks|CTS\\]\\[FAILED\\]|candidate_frontier_entries|feasible_frontier_entries' \
  result/fanout4_debug_run.log result/cts/cts.log
```

Config check:

```bash
cd /home/liweiguo/project/ecc-tools-dev
sed -n '1,80p' scripts/design/ics55_dev/iEDA_config/cts_default_config.json
sed -n '1,90p' scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl
```

Final checker:

```bash
cd /home/liweiguo/project/ecc-tools-dev
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Review Gates

- Before code changes: review whether diagnostics can be collected from current logs or require instrumentation.
- Before functional fix: review root-cause report and choose one primary fix path.
- Before completion: verify both small-fanout behavior and known-good default behavior.

## Rollback Points

- Any temporary config changes in `scripts/design/ics55_dev/iEDA_config/cts_default_config.json` must be restored or explicitly documented.
- Any debug-only code logs must be removed or converted to concise permanent diagnostics before final validation.
- Generated `result/` files are evidence, not source changes; do not rely on them as the only artifact.
