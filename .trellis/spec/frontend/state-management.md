# State Management

Shared CTS state lives in backend singletons and platform managers, not in UI-local caches.

## Rules
- Use `flowConfigInst`, `iplf::tmInst`, and `CTSAPIInst` as the source of truth for CTS flow state.
- Pass config path and work directory downward; do not duplicate parsed CTS config in UI code.
- Let GUI, Tcl, and Python remain stateless wrappers whenever possible.
- Keep persisted CTS artifacts in files or backend objects, not in duplicated interface memory.

## Examples
- `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp`
- `src/operation/iCTS/api/CTSAPI.cc`
- `src/interface/default_config/cts_default_config.json`

## Avoid
- Caching CTS tree or report data separately in each interface layer.
- Mutating backend state from GUI code without going through the shared bridge.
- Storing configuration defaults in more than one place.
