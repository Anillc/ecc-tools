# implement.md

## Implementation Checklist

- [x] Verify `post_pl` artifact files and metadata-referenced PDK files exist.
- [x] Create `scripts/design/ics55_huge_dev` from the existing `scripts/design/ics55_dev` harness.
- [x] Remove stale copied results/logs from the huge run directory.
- [x] Update `iEDA_config/db_default_config.json` for `ExampleRocketSystem` post-placement inputs and metadata-derived tlef/lef/lib/sdc paths.
- [x] Update `iEDA_config/flow_config.json` so `icts_path` points at `ics55_huge_dev`.
- [x] Reset the huge dev Tcl script to the standard DEF-based `run_iCTS_dev.tcl` flow.
- [x] Confirm the huge run directory is ignored by Git.
- [x] Rebuild `iEDA`.
- [x] Place the rebuilt binary at `scripts/design/ics55_huge_dev/iEDA`.
- [x] Run `./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` from `scripts/design/ics55_huge_dev`.
- [x] Record completion status, logs, and first actionable error if the run fails.

## Validation

- `git check-ignore -v scripts/design/ics55_huge_dev scripts/design/ics55_huge_dev/iEDA_config/db_default_config.json`
- `bash build.sh -j <n> -y`
- `cd scripts/design/ics55_huge_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`

## Review Gates

- Before activation: PRD, design, and implementation checklist exist and match the user's requested benchmark path.
- Before final report: no huge design files or generated run outputs are staged or visible as unignored Git changes.

## Execution Record

- Rebuilt binary: `bash build.sh -y` completed successfully.
- Local binary copied to: `scripts/design/ics55_huge_dev/iEDA`.
- Run command:
  `cd scripts/design/ics55_huge_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl > run_iCTS_dev.stdout.log 2>&1`
- Run exit code: `0`.
- Run log: `scripts/design/ics55_huge_dev/run_iCTS_dev.stdout.log`.
- Key timings:
  - `CTSReadData`: `25.762 s`
  - `CTSFlow`: `167.827 s`
  - `Instantiation`: `4.980 s`
  - `Evaluation`: `184.554 s`
  - main `CTS`: `383.323 s`
  - `Report`: `8.189 s`
- Key outputs:
  - `scripts/design/ics55_huge_dev/result/cts.def` (`757046983` bytes)
  - `scripts/design/ics55_huge_dev/result/cts.v` (`708647888` bytes)
  - `scripts/design/ics55_huge_dev/result/cts/cts.log`
  - `scripts/design/ics55_huge_dev/result/cts/sta/ExampleRocketSystem.rpt`
  - `scripts/design/ics55_huge_dev/result/cts/visualization/svg/cts_design.svg`
  - `scripts/design/ics55_huge_dev/result/cts/visualization/gds/cts_design.gds`
- Result directory size: `2.4G`.
- Local run directory ignore check:
  `.gitignore:28:/scripts scripts/design/ics55_huge_dev`.
- Source fixes required to complete the huge case:
  - avoid linear scans when querying iDB inst geometry during report generation
  - keep CTS `Design` name indexes authoritative for inst/net/pin lookup
  - deduplicate iDB clock-net pins with a set instead of repeated vector scans
  - add missing report CMake target dependencies for checker-visible includes
- Final checker:
  `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` completed with `0` in-scope findings.
- Spec update:
  `.trellis/spec/backend/database-guidelines.md` now records the scalable-query-path rule learned from this huge-design run.
