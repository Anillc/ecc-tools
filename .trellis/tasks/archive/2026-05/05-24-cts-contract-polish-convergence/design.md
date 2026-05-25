# CTS contract polish design

## Contract taxonomy

iCTS flow and module boundaries should expose only contracts that carry real information.

- `{Name}Input`: meaningful grouped inputs for a stage or module. Prefer direct parameters when the group would contain only one field or when the grouping hides obvious dependencies.
- `{Name}Config`: behavior-affecting knobs for the algorithm/module boundary. Do not include globally invariant data, runtime services, database units, reporters, or values that are always derived from the design/runtime context.
- `{Name}Output`: payload produced for downstream design mutation or later computation.
- `{Name}Summary`: execution observations such as status, counts, warnings, timing, metrics, or artifact paths.

When a stage produces only execution observations, its API returns `{Name}Summary` directly. When it produces only payload, it returns the payload or `{Name}Output` directly. Use both `Output` and `Summary` only if downstream code needs both categories as distinct concepts.

Stage facades with several runtime/domain dependencies should use `{Name}Input` even when every field is a reference. Explicit long parameter lists
are better than hidden singletons but still obscure the stage boundary. Do not create `{Name}Config` unless the stage has real behavior-changing
policy that is distinct from runtime `Config`.

The reporter dependency should read as CTS-level `SchemaWriter` in business signatures. Keep `schema::` qualification for report DSL types and
helpers.

HTree detailed diagnostics should be owned by HTree build/report code. HTree, topology, and source-trunk summaries should carry only caller-needed
status and aggregation fields; report-only metrics should not be nested and forwarded through production summaries.

## Existing architecture constraints

- The only singleton boundary is `CTSAPI` in `src/operation/iCTS/api`.
- Flow/module code receives `Design`, `Config`, `Wrapper`, `Reporter`, `CTSRuntime`, STA/FastSTA adapters, and algorithm dependencies explicitly.
- iCTS source must not depend on the API layer.
- Error handling uses `LOG_*` conventions rather than exceptions.
- Runtime binding should stay at flow orchestration boundaries; low-level algorithms should receive the narrow data/services they actually need.

## Options/Result convergence rule

`Options` and `Result` names are acceptable only for local implementation vocabulary where they are not part of the public flow/module contract, or where an external/lower-level algorithm convention is already clearer than the iCTS flow vocabulary. Public production flow/module boundaries should use the Input/Config/Output/Summary taxonomy unless a documented exception is more readable.

## Output responsibility

Generated design state should be written through explicit `Design`/`Wrapper` references at the stage that owns the mutation. Summaries should report what happened, not carry the mutated design. Reporters should be passed explicitly where a report is emitted rather than hidden in broad result objects.

Detailed diagnostics are not output payload. They may be emitted immediately by the owning stage or kept in local build/report state while needed,
but should not be returned to upstream callers unless the caller uses them for control flow or flow-level aggregation.

## Rollback shape

The change is source-compatible only inside iCTS. If a contract rename or deletion causes a broad build break, revert that individual contract change and record it as a justified exception in the audit rather than weakening the global rule.
