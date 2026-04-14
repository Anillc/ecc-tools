# Fix ICS55 Dev CTS Script

## Goal
Make the iCTS development Tcl script initialize iDB correctly by reading the required LEF and DEF paths from `db_default_config.json`.

## Requirements
- Keep using the existing `db_default_config.json` as the source of truth for DB input paths.
- Load `tech_lef_path`, `lef_paths`, and `def_path` from the JSON before `run_cts`.
- Avoid duplicating hardcoded DB input paths in the Tcl script.
- Limit the change to the development script unless a supporting task file update is required.

## Acceptance Criteria
- [ ] `run_iCTS_dev.tcl` reads the required DB paths from `db_default_config.json`.
- [ ] The script performs `tech_lef_init`, `lef_init`, and `def_init` before `run_cts`.
- [ ] The script no longer hardcodes the DEF input path directly in the `def_init` call.

## Technical Notes
- `db_init` only populates `dmInst->get_config()` and does not build iDB objects.
- `def_init` requires LEF data to be initialized first.
- The script should stay self-contained and not depend on external environment setup.
