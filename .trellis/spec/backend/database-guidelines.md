# Database Guidelines

This project does not use an ORM in CTS. "Database" usually means IDB objects plus CTS-side mirrors.

## Rules
- Read and write layout data through `CtsDBWrapper` or platform services.
- Keep CTS-side state in `CtsDesign`, `CtsNet`, `CtsPin`, `CtsInstance`.
- Parse runtime knobs in `CtsConfig` and `JsonParser`, not in Tcl or solver code.
- Mark imported objects consistently, for example `set_is_newly(false)` after IDB reads.
- Preserve CTS<->IDB cross references inside wrapper code.

## Examples
- `src/operation/iCTS/source/data_manager/io/CtsDBWrapper.cc`
- `src/operation/iCTS/source/data_manager/database/CtsDesign.cc`
- `src/operation/iCTS/source/data_manager/config/CtsConfig.hh`
- `src/platform/data_manager/config/dm_cts_config.cpp`

## Avoid
- Accessing `IdbBuilder` directly from solver or tree-builder code.
- Passing raw JSON blobs deep into synthesis code.
- Duplicating the same net or pin lookup logic in multiple layers.
