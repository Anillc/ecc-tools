# run ics55 huge post-pl design through iCTS

## Goal

Create a local ignored `scripts/design/ics55_huge_dev` run directory for the RocketChip ics55 post-placement benchmark, configure it from the Innovus `post_pl` artifact metadata, reuse the freshly rebuilt `iEDA`, and run the iCTS development script against the huge design to determine whether the current binary can complete the flow.

## Background / Known Context

- User normally runs the smaller/dev setup with:
  `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- Current requested source artifact directory:
  `/nfs/share/home/huangzhipeng/code-new/benchmark_ecc/benchmark_run/ics55_300W/rocketchip_400W/artifacts/innovus/post_pl`
- Previous XiangShan source artifact directory was abandoned by user request and is no longer the target.
- `post_pl` contains `design.def`, `design.v`, `constraints.sdc`, and `stage_metadata.json`.
- `stage_metadata.json` provides the technology and library paths needed by iEDA config:
  tlef `/nfs/share/home/huangzhipeng/code-new/icsprout55-pdk/lef/N551P6M_ecos.lef`;
  six LEF paths under `/nfs/share/home/huangzhipeng/code-new/icsprout55-pdk/lef`;
  six NLDM `.lib` paths under `/nfs/share/home/huangzhipeng/code-new/icsprout55-pdk/lib`.
- The DEF design name is `ExampleRocketSystem`, with 1117878 components and 1120357 nets.
- The SDC creates clock `io_aggregator_0_clock` with 10 ns period.
- Repository root `.gitignore` already ignores `/scripts` and `ics55_*`; `scripts/design/ics55_dev/.gitignore` also ignores generated design artifacts and local binaries.

## Requirements

- Add a local `scripts/design/ics55_huge_dev` run directory derived from the existing `scripts/design/ics55_dev` structure.
- Keep `scripts/design/ics55_huge_dev` ignored so huge design data, local binaries, logs, and result files are not committed.
- Configure the huge run directory to use the `post_pl` design inputs and metadata-derived tlef/lef/lib/sdc values.
- Use the freshly rebuilt `iEDA` binary from this repository before running the benchmark.
- Run the iCTS development command from the huge run directory:
  `./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- Capture whether the flow completes, fails, or times out, including the first actionable error if it does not complete.

## Acceptance Criteria

- [x] `scripts/design/ics55_huge_dev` exists locally and is ignored by Git.
- [x] `scripts/design/ics55_huge_dev/iEDA_config/db_default_config.json` points to the `post_pl` DEF, Verilog, SDC, metadata tlef, LEFs, and LIBs.
- [x] `scripts/design/ics55_huge_dev/iEDA_config/flow_config.json` points its `icts_path` at the huge run directory, not the original `ics55_dev`.
- [x] A freshly rebuilt `iEDA` binary is available to the huge run directory.
- [x] The iCTS dev script has been executed and the result is documented with log/output paths.

## Definition of Done

- Build command, binary reuse, and run command have been recorded in the final report.
- Any source-controlled Trellis/task edits are visible in `git status`.
- No huge benchmark artifacts are added to Git.

## Out of Scope

- Optimizing CTS runtime or QoR.
- Changing iCTS algorithms unless the run exposes a blocker that must be fixed to start the flow.
- Committing the task or generated benchmark files.
