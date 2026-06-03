# Fix CTS Design removal index performance

## Goal

Remove avoidable full-map scans from iCTS `Design` topology-object removal so name indexes stay consistent through targeted erasure.

## Requirements

- Scope is limited to `src/operation/iCTS/source/database/design/Design.*` unless a directly required test/build adjustment is discovered.
- Preserve existing ownership semantics: `Design` still owns final `Clock`, `Inst`, `Pin`, and `Net` objects through `std::unique_ptr`.
- Preserve existing `findInst`, `findPin`, and `findNet` behavior.
- Replace removal-time map scans for inst/net name indexes with targeted erasure based on the object's current name.
- Keep pin reverse-index handling intact.
- Do not change CTS flow behavior, generated topology, or report semantics.
- Validate with the requested iCTS dev script and a full iCTS `ecc_dev_tools` check.

## Acceptance Criteria

- [x] `Design::removeInst` no longer scans `_inst_by_name` to remove a single inst.
- [x] `Design::removeNet` no longer scans `_net_by_name` to remove a single net.
- [x] `Design` name indexes remain correct when removing clock membership objects.
- [x] `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` succeeds.
- [x] `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` succeeds.

## Notes

- User requested a separate follow-up task for spec updates; do not mix spec edits into this task.
