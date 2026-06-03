# Implementation Plan

## Steps

- Inspect `Design` ownership and index update paths.
- Replace inst/net removal scans with targeted erasure helpers.
- Build or run focused validation if needed.
- Run the requested iCTS dev flow:
  `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- Run the full iCTS checker:
  `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
- Commit only this code task and task artifacts.

## Rollback Point

- Revert only the `Design` index-removal changes if validation shows behavior drift.
