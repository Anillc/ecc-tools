# design.md

## Technical Design

The run directory will be created under `scripts/design/ics55_huge_dev` by copying the existing local `scripts/design/ics55_dev` harness. This preserves the Tcl script layout, default iEDA config names, and existing `run_iCTS_dev.tcl` behavior while isolating huge-design inputs and results.

The huge directory will use absolute input paths for benchmark artifacts and PDK files. This avoids copying hundreds of megabytes of design data into the repository and keeps the run reproducible against the supplied NFS artifact location.

Required DB config mapping:

- `INPUT.tech_lef_path`: `/nfs/share/home/huangzhipeng/code-new/icsprout55-pdk/lef/N551P6M_ecos.lef`
- `INPUT.lef_paths`: the six `lef_paths` entries from `stage_metadata.json`
- `INPUT.def_path`: `/nfs/share/home/huangzhipeng/code-new/benchmark_ecc/benchmark_run/ics55_300W/rocketchip_400W/artifacts/innovus/post_pl/design.def`
- `INPUT.verilog_path`: `/nfs/share/home/huangzhipeng/code-new/benchmark_ecc/benchmark_run/ics55_300W/rocketchip_400W/artifacts/innovus/post_pl/design.v`
- `INPUT.lib_path`: the six `lib_paths` entries from `stage_metadata.json`
- `INPUT.sdc_path`: `/nfs/share/home/huangzhipeng/code-new/benchmark_ecc/benchmark_run/ics55_300W/rocketchip_400W/artifacts/innovus/post_pl/constraints.sdc`

`flow_config.json` must be updated so `ConfigPath.icts_path` references `scripts/design/ics55_huge_dev/iEDA_config/cts_default_config.json`.

The run uses the standard `run_iCTS_dev.tcl` flow derived from `ics55_dev`: read tech LEF, LEFs, DEF, then run CTS. The abandoned XiangShan-only `verilog_init -top XSTop` experiment is not part of the RocketChip run.

The rebuilt binary will be produced with the repository build script and then made available as `scripts/design/ics55_huge_dev/iEDA` so the user-facing command shape remains the same as the existing `ics55_dev` workflow.

## Compatibility / Risk

The existing `run_iCTS_dev.tcl` reads LEF and DEF and then runs CTS. iCTS consumes LIB and SDC from the DB/runtime config. If RocketChip's post-PL DEF clock net is incomplete, the run result should report that as the next blocker rather than carrying over XiangShan-specific workarounds.

The benchmark is large enough that runtime or memory may be significant. The task's success criterion is to run the current binary and report the actual outcome, not to guarantee QoR or completion within a short time.

## Rollout / Rollback

All benchmark setup files live under an ignored local directory. Rollback is removing `scripts/design/ics55_huge_dev` and any generated logs/results. Trellis task artifacts remain source-controlled planning/trace records.
