# CTS Report Visualization Research

## Latest Scope

Report-stage CTS visualization is SVG-only.

- Produce `cts_design.svg` and `cts_flyline.svg`.
- Do not produce or log `cts_design.gds`, `cts_flyline.gds`, or `cts_report.lyp`.
- Do not add Wrapper physical layout facts, GDS semantic layers, layer-property sidecars, report DAG helpers, or visualization-only metadata propagation.
- Log only basic artifact path/status/detail rows in `cts.log`.
- Move reusable SVG plotter/helpers from test into production `source/utils`.

## Current Branch Entry Points

- `CTSAPI::report(const std::string& save_dir)` delegates to `FLOW_MANAGER_INST.report(save_dir)`.
- `FlowManager::report(...)` writes statistics reports, emits `CTS Report Mode` / `CTS Report Runtime`, and calls report visualization generation.
- Visualization artifacts should be written under `<report_root>/output`, where `report_root` is the report save dir when provided or the configured CTS work dir.

## SVG Reference

Existing test SVG code before the move:

- `src/operation/iCTS/test/common/visualization/core/SvgCommon.hh`
- `src/operation/iCTS/test/common/visualization/cluster/ClusterSvgWriter.*`
- `src/operation/iCTS/test/common/visualization/topology/TopologySvgWriter.*`

Adaptation notes:

- Move reusable SVG transform/formatting/writer code to production `source/utils/visualization`.
- Update test CMake/includes to link/use the moved production utility.
- Avoid leaving a duplicated test-only plotter.
- Use `Router::buildClockNetTree` for `cts_design.svg` so the design view follows routed CTS tree construction.
- Use direct driver-to-load segments only for `cts_flyline.svg`.

## Validation

Suggested command:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Expected artifacts:

- `result/cts/output/cts_design.svg`
- `result/cts/output/cts_flyline.svg`

Expected log:

- `result/cts/cts.log` contains a report visualization artifact table with path and status for the two SVG files.

Latest user instruction for this cleanup explicitly forbids `ecc_dev_tools`; do not run it unless the user changes that instruction.
