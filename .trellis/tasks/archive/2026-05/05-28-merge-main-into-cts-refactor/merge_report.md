# Merge Report

## Scope

- Target branch: `cts_refactor`
- Merge source: `origin/main` at `493c1ea777e13c723b618158b6029f32a89e97b8`
- Pre-merge HEAD: `3e639d49e6fb14cb05cc715a6c17e68db845a8da`
- Safety branch: `backup/cts-refactor-before-main-merge-20260528`
- Merge mode: `git merge --no-commit --no-ff origin/main`

## CTS Resolution

- Kept current `cts_refactor` iCTS architecture as authoritative.
- Removed 111 accidentally staged main-side old/flat iCTS additions after final checker exposed them as unused stale CTS files.
- Kept only 6 CTS-file changes in the staged merge result:
  - `src/operation/iCTS/source/database/adapter/sdc/clock_trace/ClockTraceResolve.cc`
  - `src/operation/iCTS/source/database/adapter/sdc/clock_trace/SdcClockTraceAlgorithm.hh`
  - `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc`
  - `src/operation/iCTS/source/database/io/WrapperClockReader.cc`
  - `src/operation/iCTS/source/database/io/WrapperClockWriter.cc`
  - `src/operation/iCTS/test/flow/FlowSdcTraceTest.cc`

## Main-Side CTS Fixes Ported

- `STAAdapter.cc`: preserved the main-side loaded-liberty check and always reinstall the Timing IDB adapter before CTS RC queries.
- `WrapperClockWriter.cc`: ported compatible safe iDB wrapper usage for pin/net/instance create, connect, disconnect, place, replace, and remove operations.
- Liberty parser compatibility: updated CTS SDC trace/read/test code to use the C++ Liberty parser types from current main.
- `source/database/io/CMakeLists.txt`: reviewed main-side commits `77149cd71` and `a08e56206`; net effect was intentionally not kept.

## External Mainline Changes Accepted

- Accepted non-CTS mainline changes where they do not alter validated CTS behavior, including script layout reorganization, iRCX, database, iSTA, interface, third-party, workspace, build, and packaging updates.
- Preserved current Trellis `AGENTS.md` instructions even though main deleted the file.
- Preserved `iEDA` output to `scripts/design/ics55_dev` so the existing CTS validation script continues to run directly from that directory.

## Final Validation

| Check | Status | Artifact |
| --- | --- | --- |
| Unmerged paths | 0 | `artifacts/merge/final_unmerged_paths.txt` |
| Conflict marker heads/tails | 0 matches | `artifacts/merge/final_conflict_marker_scan_heads_tails.txt` |
| Unstaged `git diff --check` | 0 | `artifacts/merge/final_git_diff_check.txt` |
| Staged `git diff --cached --check` | 2 | `artifacts/merge/final_git_diff_cached_check.txt` |
| `cmake --build build -j 32 --target iEDA` | 0 | `artifacts/build/build_iEDA_after_vector_include.log` |
| iCTS final reference flow | 0 | `artifacts/post_merge/icts_final.log` |
| `ecc_dev_tools` iCTS full check | 0 | `artifacts/build/ecc_dev_tools_icts_final_after_vector_include.log` |

The staged whitespace check remains non-zero because of trailing whitespace and blank-EOF issues inherited from accepted mainline files, mostly `scripts/design`, `src/database`, and Liberty parser files. The manually touched iCTS files are formatted and the iCTS full checker passes.

## CTS Metric Comparison

| Metric | Pre-merge | Final |
| --- | ---: | ---: |
| status | finished | finished |
| clock_count | 1 | 1 |
| sink_count | 8751 | 8751 |
| selected_htree_level_count | 11 | 11 |
| htree_inserted_buffer_count | 806 | 806 |
| final_clock_buffer_count | 2996 | 2996 |
| final_buffer_area | 8537.760 um^2 | 8537.760 um^2 |
| max_clock_net_wirelength | 359.608 um | 359.608 um |
| total_clock_network_wirelength | 61790.733 um | 61790.733 um |

## Commit Status

The merge result is resolved and staged but not committed. The remaining step is user approval to create the merge commit on `cts_refactor`.
