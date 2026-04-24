# Database Guidelines

This project does not use an ORM in CTS. "Database" usually means IDB objects plus CTS-side mirrors.

## Rules
- Read and write layout data through `CtsDBWrapper` or platform services.
- Keep CTS-side state in `CtsDesign`, `CtsNet`, `CtsPin`, `CtsInstance`.
- Parse runtime knobs in `CtsConfig` and `JsonParser`, not in Tcl or solver code.
- Mark imported objects consistently, for example `set_is_newly(false)` after IDB reads.
- Preserve CTS<->IDB cross references inside wrapper code.
- Prefer explicit wrapper contracts such as `read(CtsDesign*)` and `writeDef(path)` over hidden singleton lookups.
- Only call no-argument wrapper entrypoints after the required runtime context has been injected, for example through `setRuntimeContext`, `setReadContext`, `setOutputDefPath`, and `setFlipFlopChecker`.
- `CtsDBWrapper` may depend on injected callbacks such as `IsFlipFlopFn`, but it must not reach upward into `CTSAPIInst` to pull config, design, or STA state.

## Examples
- `src/operation/iCTS/source/data_manager/io/CtsDBWrapper.cc`
- `src/operation/iCTS/source/data_manager/database/CtsDesign.cc`
- `src/operation/iCTS/source/data_manager/config/CtsConfig.hh`
- `src/platform/data_manager/config/dm_cts_config.cpp`

## Good / Base / Bad
- Good: `CTSFlowRunner` injects the runtime context once, then `CtsDBWrapper::read()` and `writeDef()` run with fully configured state.
- Base: the caller bypasses cached context and uses `read(design)` or `writeDef(path)` with explicit arguments.
- Bad: a caller invokes no-argument `read()` or `writeDef()` before context injection; this should fail fast with `LOG_FATAL` or `LOG_FATAL_IF`.

## Avoid
- Accessing `IdbBuilder` directly from solver or tree-builder code.
- Passing raw JSON blobs deep into synthesis code.
- Duplicating the same net or pin lookup logic in multiple layers.
- Reintroducing wrapper-to-API singleton backedges after the wrapper has been purified.
