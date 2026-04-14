# Error Handling

CTS backend mainly uses logging macros plus guard clauses, not exception-heavy flows.

## Rules
- Use `LOG_FATAL` or `LOG_FATAL_IF` for broken invariants, missing required inputs, or impossible states.
- Use `LOG_WARNING` for recoverable data quality issues, such as duplicate sink locations.
- Use early return for empty or optional work; do not escalate normal no-op paths.
- Validate Tcl command options in `check()` before executing backend work.
- Keep failure messages actionable: include net name, pin name, path, or config key.

## Examples
- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/source/solver/Solver.cc`
- `src/operation/iCTS/source/solver/tools/tree_builder/local_legalization/LocalLegalization.cc`
- `src/interface/tcl/tcl_icts/tcl_cts.cpp`

## Avoid
- Silent `false` returns without a matching log.
- Throwing a new error style only inside CTS while the rest of the module uses logging macros.
- Hiding invalid geometry or null builder states until much later in the flow.
