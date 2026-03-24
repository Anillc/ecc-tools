# Database Guidelines

Ownership, singleton boundaries, and data-model rules for iCTS.

## Scope

This document covers singleton roles, ownership, lifetime, database-layer placement, and new data classes.

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
- Module code should use narrowed singletons such as `CONFIG_INST`, `DESIGN_INST`, `WRAPPER_INST`, and `STA_ADAPTER_INST`.
- Do not introduce new singleton boundaries without a clear cross-module need.

### Ownership

- Use `std::unique_ptr` for ownership.
- Use raw pointers only for non-owning cross-references.
- `Design` owns `Clock` objects.
- `Wrapper` owns CTS-side `Pin`, `Inst`, and `Net` objects created from iDB.
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
- [ ] Header-only database types use `INTERFACE` targets when appropriate
- [ ] New singleton usage is truly cross-module and justified

## Related Docs

- `directory-structure.md`
- `quality-guidelines.md`
- `../guides/cross-layer-thinking-guide.md`
