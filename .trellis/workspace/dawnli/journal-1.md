# Journal - dawnli (Part 1)

> AI development session journal
> Started: 2026-04-14

---



## Session 1: Bootstrap CTS-oriented Trellis guidelines

**Date**: 2026-04-14
**Task**: Bootstrap CTS-oriented Trellis guidelines
**Branch**: `cts_fix`

### Summary

Filled backend, frontend, and guides specs with concise CTS-oriented conventions, initialized task context files, and aligned the bootstrap task PRD with the delivered scope.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `a7189533e` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 2: Fix ICS55 Dev CTS Debug Script

**Date**: 2026-04-14
**Task**: Fix ICS55 Dev CTS Debug Script
**Branch**: `cts_fix`

### Summary

Updated the local ICS55 iCTS debug Tcl script to read LEF and DEF paths from db_default_config.json and initialize LEF before def_init so CTS no longer reaches the null idb_builder path.

### Main Changes

| Area | Description |
|------|-------------|
| Debug Script | Updated `scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl` to compute local config paths, read `tech_lef_path`, `lef_paths`, and `def_path` from `db_default_config.json`, and run `tech_lef_init`, `lef_init`, and `def_init` before `run_cts`. |
| Verification | Performed Tcl syntax validation locally. The user tested separately and committed the code in `eda0cb7c8`. |
| Notes | The script is debug-only and lives under the ignored `scripts/` tree, so task and session metadata are the source of project memory for this change. |


### Git Commits

| Hash | Message |
|------|---------|
| `eda0cb7c8` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 3: Stabilize iCTS H-tree CTS flow

**Date**: 2026-04-15
**Task**: Stabilize iCTS H-tree CTS flow
**Branch**: `cts_fix`

### Summary

(Add summary)

### Main Changes

| Area | Summary |
|------|---------|
| Topology | Reworked iCTS clock synthesis around sink MCF+kmeans clustering, top-down H-tree buffer topology, and break-long-wire buffering with `min_buffering_length`. |
| Sizing | Replaced fatal empty-feasible-set behavior with fallback selection on the global delay/area/power Pareto front when the active CTS constraints have no feasible sizing candidate. |
| Config & Logging | Simplified CTS config surface, added deprecation warnings for removed keys, and kept algorithm-level summaries plus key RC / pin statistics in logs. |
| Validation | Rebuilt `iEDA` and reran `scripts/design/ics55_dev/run_iCTS_dev.tcl`; the arm9/ics55 flow completed and produced DEF / Verilog / STA / GDS outputs. |

**Run Notes**:
- The latest verified run completed successfully on `2026-04-15 11:51:48 +0800`.
- This testcase still has `feasible: 0` under the current skew constraint set; fallback now selects from the global delay/area/power Pareto front instead of collapsing to the least-violation all-`BUFX20H7L` assignment.
- The selected fallback sizing for `clk` was `d0=BUFX20H7L, d1=BUFX20H7L, d2=BUFX20H7L, d3=BUFX12H7L, d4=BUFX8H7L, d5=BUFX8H7L, d6=BUFX8H7L`.
- A local-only path tweak in `src/apps/CMakeLists.txt` remains uncommitted in the workspace and was not part of the recorded feature commit.


### Git Commits

| Hash | Message |
|------|---------|
| `d2d9d93c6` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 4: iCTS architecture refactor closure

**Date**: 2026-04-16
**Task**: iCTS architecture refactor closure
**Branch**: `cts_fix`

### Summary

Completed the iCTS architecture refactor, finalized H-tree DRV-driven stop conditions and report logging behavior, reverted external-module intrusion in IdbLayer.h, and updated spec constraints to keep verification/cleanup scoped to iCTS.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `7da768828` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete
