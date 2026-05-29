# Design

## Problem

iCTS source code is the same on the current branch and `reduce_tools`, but the final `ecc_bin` link graph differs. The current branch includes additional removed-tool targets that cause CMake to repeat iCTS static archives more times in the link command. `reduce_tools` has a smaller link closure and exposes iCTS archive order problems.

The known reduced-link failures are all provider-after-consumer issues:

- `IdbConversion.cc` consumes `Wrapper::writeClocksDetailed`, implemented by `icts_source_database_io`.
- `SinkBranch.cc` and `SourceTrunk.cc` consume `CharacterizationLibrary::*`, implemented by `icts_source_flow_synthesis_htree_characterization_library`.
- `SinkBranch.cc` and `SourceTrunk.cc` consume `HTree::build`, implemented by `icts_source_flow_synthesis_htree`.
- `CTSAPI.cc` consumes `Flow::*`; the direct owner is `tool_api_icts -> icts_api`, not `tool_manager -> icts_source`.
- `FastStaPower.cc` and `FastStaDmpCeffShared.cc` consume `FastStaLibertyTable::lookup`, previously implemented inside the broader Liberty archive. That archive was also needed by earlier consumers, so CMake emitted it before the late fast-STA power/timing archives under a one-pass static link.

The fix must make the iCTS target graph correct by itself, instead of relying on unrelated tool targets to create extra archive scans.

## Modification Scope

Primary expected files:

- `src/operation/iCTS/source/flow/instantiation/CMakeLists.txt`
- `src/operation/iCTS/source/flow/instantiation/idb_conversion/CMakeLists.txt`
- `src/operation/iCTS/source/flow/synthesis/CMakeLists.txt`
- `src/operation/iCTS/source/flow/synthesis/topology/CMakeLists.txt`
- `src/operation/iCTS/source/flow/synthesis/topology/sink/CMakeLists.txt`
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/CMakeLists.txt`
- `src/operation/iCTS/source/flow/CMakeLists.txt`
- `src/operation/iCTS/source/database/adapter/fast_sta/CMakeLists.txt`
- `src/platform/tool_manager/tool_api/icts_io/CMakeLists.txt`
- `src/platform/tool_manager/CMakeLists.txt`

Source-code edits:

- No `.cc` behavior changes are planned.
- Header/source behavior must stay unchanged; CMake-only repairs are preferred unless a source include is proven wrong.
- No `src/interface/python` changes are part of this task.
- No top-level CMake or tool-manager changes are planned unless validation disproves the iCTS-only fix.

## Preferred Fix

Use target dependency repair only. Link-group rescans and artificial duplicate archive scans are disallowed for this task.

### 1. Express direct dependencies at the consumer target

Add or correct direct target dependencies where the object files directly reference symbols:

- `icts_source_flow_instantiation_idb_conversion`
  - already depends on `icts_source_database_io`; keep the dependency local to the consumer.
- `icts_source_flow_synthesis_topology_sink`
  - already depends on `icts_source_flow_synthesis_htree` because it calls `HTree::build`.
  - add `icts_source_flow_synthesis_htree_characterization_library` because it calls `CharacterizationLibrary::buildRuntimeInput` and `buildRuntimeConfig`.
- `icts_source_flow_synthesis_topology_trunk`
  - keep `icts_source_flow_synthesis_htree`; its public header exposes `HTree` types, so `PUBLIC` is defensible.
  - keep/add `icts_source_flow_synthesis_htree_characterization_library` as an implementation dependency because its public header only forward-declares `CharacterizationLibrary`.

### 2. Break order sensitivity in aggregate targets

For static archives, the final link line must not rely on coincidental repeated scans. Reorder high-level iCTS aggregate links and clarify direct dependencies so every target's object files can resolve symbols through its declared dependencies without requiring unrelated upstream tools to repeat iCTS archives.

Expected order intent:

- In instantiation:
  - `icts_source_flow_instantiation_design_conversion`
  - `icts_source_flow_instantiation_idb_conversion`
  - then `icts_source_database_io`
- In topology:
  - `icts_source_flow_synthesis_topology_sink`
  - `icts_source_flow_synthesis_topology_trunk`
  - then `icts_source_flow_synthesis_htree`
  - then `icts_source_flow_synthesis_htree_characterization_library`
- In synthesis and flow aggregates:
  - flow-level consumer stages first.
  - shared providers (`database_io`, fast STA adapter, characterization library) after consumers where practical.
- In fast STA:
  - split the lookup implementation from the broader Liberty extraction archive into `icts_source_database_adapter_fast_sta_liberty_model`.
  - link `timing` and `power` to that narrow lookup provider.
  - keep `FastStaLiberty::extractBufferCell` dependencies on the targets that actually call it.
- In tool manager:
  - remove broad `tool_manager -> icts_source`.
  - add the direct `tool_api_icts -> icts_api` dependency because `icts_io.cpp` includes and calls `CTSAPI`.

Use split `target_link_libraries` calls when needed to make CMake preserve a clear dependency expression while retaining accurate `PUBLIC` vs `PRIVATE` scope. Do not use link groups.

### 3. Keep `PUBLIC` minimal

Scope rule:

- Use `PUBLIC` only for dependencies needed by public headers or downstream consumers.
- Use `PRIVATE` for dependencies used only by `.cc` files.

Examples:

- `Topology.hh` includes and exposes `HTree::LogContext`; `icts_source_flow_synthesis_topology` may need `icts_source_flow_synthesis_htree` as `PUBLIC`.
- `SinkBranch.hh` does not expose `CharacterizationLibrary`, so `icts_source_flow_synthesis_htree_characterization_library` should be `PRIVATE` for `topology_sink`.
- `SourceTrunk.hh` forward-declares `CharacterizationLibrary`, so the characterization library can remain `PRIVATE` for `topology_trunk`.

## Disallowed Fixes

- Do not use CMake `LINK_GROUP:RESCAN`.
- Do not add linker start/end groups.
- Do not intentionally duplicate iCTS archives on the final link line as a workaround.
- Do not add removed tools back into `reduce_tools` to create incidental extra archive scans.

## Validation Strategy

1. Build current branch `ecc_bin`.
2. Inspect current branch `ecc_bin` link command for iCTS unresolved-risk ordering.
3. Run `ics55_dev` with the newly built binary copied or linked to `scripts/design/ics55_dev/iEDA`:
   - `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
4. Apply the same iCTS patch to `/tmp/ecc-tools-reduce-tools`.
5. Build `ecc_bin` in `/tmp/ecc-tools-reduce-tools`.
6. If reduced `ecc_bin` still fails with iCTS unresolved symbols, continue dependency-cycle analysis and repair the iCTS target graph. Do not add fallback link-group behavior.
7. As an additional strictness check, rebuild the final link command after removing duplicate `libicts_source*.a` entries. This confirms the result is not relying on repeated iCTS archive scans.

## Risks

- Static archive cycles may extend beyond the currently observed unresolved symbols. If so, continue splitting or clarifying iCTS target dependencies until the cycle is removed.
- Reordering aggregate targets can expose missing dependencies in other iCTS subtargets. Treat any new unresolved symbol as evidence of a missing direct dependency, not as a reason to add unrelated tools back.
- `ics55_dev` runtime validation depends on local PDK availability at `/home/liweiguo/PDK/icsprout55-pdk`.
