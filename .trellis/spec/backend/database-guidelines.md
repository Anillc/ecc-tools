# Database Guidelines

Ownership, singleton boundaries, and data-model rules for iCTS.

## Scope

This document covers singleton roles, ownership, lifetime, database-layer placement, and external adapter boundaries.
Naming and generic accessor style live in `quality-guidelines.md`.

## Rules

### Singleton Roles

Use the existing singleton boundaries:

| Macro | Role |
|-------|------|
| `CTS_API_INST` | External API entry point |
| `DESIGN_INST` | Design database |
| `CONFIG_INST` | Configuration |
| `WRAPPER_INST` | iDB adapter |
| `STA_ADAPTER_INST` | iSTA adapter for internal source-layer use |
| `LOG_INST` | Logger |

Rules:
- External callers enter through `CTS_API_INST`.
- API, flow, database, and adapter boundaries may read `CONFIG_INST` when they own initialization or external-tool setup.
- Algorithm code under `source/module/` should receive runtime configuration through existing Options/Config parameters instead of reading `CONFIG_INST` directly.
- Do not introduce new singleton boundaries without a clear cross-module need.

### Ownership

- Use `std::unique_ptr` for ownership.
- Use raw pointers only for non-owning cross-references and topology edges.
- `Design` owns final CTS `Clock`, `Inst`, `Pin`, and `Net` objects.
- `Clock` owns no final `Inst`, `Pin`, or `Net` objects. It stores only borrowed pointers for per-clock anchors and final membership views, such as the clock source pin, clock source net, original sinks, clock insts, and clock nets.
- `Inst` owns no pins. Pin accessors expose only borrowed pointer views and ordering/query helpers.
- Algorithm-local result objects may own temporary `Inst`, `Pin`, and `Net` objects while an operation is in progress. Commit temporary objects into `Design` only after success; failed temporary results must destruct without changing final `Design` or `Clock` state.
- `Net` driver/load pointers and `Pin` inst/net pointers are non-owning topology edges. Do not turn them into ownership links.
- `Wrapper` may keep cross-reference maps between iDB objects and CTS objects, but those maps borrow CTS pointers; `Wrapper` does not own per-clock topology objects.
- `Tree` owns `TreeNode` objects.
- Borrowed pointers must not outlive the owner.
- Do not cache borrowed pointers across owner reset boundaries.

### Placement

Put new types in the narrowest database subdirectory that matches their role:
- config types -> `source/database/config/`
- design objects -> `source/database/design/`
- iDB adapter code -> `source/database/io/`
- iSTA adapter code -> `source/database/adapter/sta/`
- spatial types -> `source/database/spatial/`
- routing DB types -> `source/database/routing/`
- timing DB types -> `source/database/timing/`

If a type is shared across modules and is part of the stable data model, prefer `source/database/` over `source/module/`.

### Access Boundaries

- Validate critical singleton state at initialization boundaries.
- Avoid scattering the same null-check pattern across modules.
- Keep iDB access inside `Wrapper`.
- Keep iSTA access inside `STAAdapter`.
- Module code should operate on CTS types, not external-tool types.
- Only synthesis/instantiation boundaries may commit CTS-created topology into `Design` or project final CTS objects through `Wrapper`/iDB.
- Evaluation, report, and visualization are readonly consumers of committed CTS results.
- Report-only data should be narrow and typed. Do not add broad snapshots that duplicate data already available from `Design`, `Clock`, `Inst`, `Net`, report metadata, or narrow `Wrapper` queries.
- Raw iDB pointers must not escape `Wrapper`.

### Scalable Query Paths

- Name-based `Design` lookups such as `findInst`, `findNet`, and full-name `findPin` must use maintained indexes as the authoritative query path. Do not add vector-scan fallback logic to these hot lookups; it hides indexing bugs and turns report/evaluation paths into O(N) or worse behavior on million-instance designs.
- When object names can change after insertion, maintain a reverse index or equivalent targeted removal path so re-indexing does not scan the entire object map.
- `Wrapper` queries over iDB objects should use iDB-provided maps/search helpers, such as `find_instance`, when available. Avoid linear scans over all iDB instances from report, evaluation, or visualization code.
- If a required index is missing or stale, fix the index ownership/update path rather than compensating at each query call site.

### Output Directories

- Flow/session code derives report roots from the runtime work directory.
- Visualization output is rooted at `visualization_dir`; statistics output is rooted at `statistics_dir`.
- Format-specific subdirectories may live below those roots.
- Remove legacy or unused output-path config fields once they are confirmed unused.

### Adding New Data Classes

When adding a new database-layer type:
1. Place it under the correct `source/database/` subdirectory.
2. Use `enum class` for enums.
3. Initialize members with sensible defaults.
4. Use an `INTERFACE` target if the type is header-only.
5. Add a real library target only when `.cc` implementation is needed.
6. Document any non-trivial ownership rule.

### Singleton Implementation

When a singleton is justified:
- use the existing Meyers Singleton pattern
- delete copy and move operations
- expose access through the established macro alias
- keep initialization order controlled by the existing API/setup flow

Do not introduce ad-hoc global state outside this pattern.

## Checklist

Before handoff, verify:

- [ ] Ownership is explicit and minimal
- [ ] Borrowed pointers do not outlive their owners
- [ ] New data types live in the correct database subdirectory
- [ ] External-tool access stays inside adapter layers
- [ ] Hot name-based queries use maintained indexes, not fallback full-object scans
- [ ] Evaluation/report code is readonly with respect to iDB projection
- [ ] Report-only data is narrow, typed, and not a broad snapshot of database state
- [ ] Header-only database types use `INTERFACE` targets when appropriate
- [ ] New singleton usage is truly cross-module and justified

## Related Docs

- `directory-structure.md`
- `quality-guidelines.md`
- `../guides/cross-layer-thinking-guide.md`
