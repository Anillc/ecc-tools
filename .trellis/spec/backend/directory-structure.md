# Directory Structure

Code placement and CMake structure rules for `src/operation/iCTS/`.

## Scope

This document covers layer boundaries, code placement, target naming, and module integration.

## Rules

### Layers

iCTS uses three layers:

| Layer | Directory | Role |
|------|-----------|------|
| API | `api/` | External entry points |
| Source | `source/` | Internal implementation |
| Test | `test/` | Tests mirroring source structure |

Dependency direction:
- API depends on Source
- Test depends on API and Source
- Source must not depend on API

### Source Categories

Use these top-level categories under `source/`:

| Category | Purpose |
|----------|---------|
| `database/` | Data model, config, IO adapters, shared DB-facing types |
| `utils/` | Shared utilities such as logger and geometry helpers |
| `module/` | Algorithms and CTS modules |
| `flow/` | Flow orchestration |

### Placement

- Put public external entry points in `api/`.
- Put internal algorithm code in `source/module/`.
- Put shared data and adapters in `source/database/`.
- Put reusable helper code in `source/utils/`.
- Put tests under `test/`, mirroring the source layout as closely as practical.

Examples:
- routing algorithm -> `source/module/routing/`
- geometry helper -> `source/utils/geometry/`
- shared DB type -> `source/database/...`
- module test -> `test/module/...`

- Keep `api/CTSAPI` focused on external entry points; internal CTS flow lifecycle steps belong under `source/flow`.
- Keep runtime config access at API/flow/database/test boundaries; code under  `source/module/` should receive explicit options instead of reading `CONFIG_INST`.

### CMake Target Naming

Use hierarchical target names:

```text
icts_{tier}_{category}_{module}
```

Examples:
- `icts_api`
- `icts_source_database_config`
- `icts_source_database_adapter_sta`
- `icts_source_module_topology`
- `icts_source_utils_logger`

### Adding Files or Modules

When adding files to an existing module:
1. Add the new `.cc` file to the module `CMakeLists.txt`.
2. Expose headers through the module target or its interface target.
3. Rebuild before adding larger implementation changes.
4. Add tests under the mirrored `test/` path when needed.

When adding a new module or submodule:
1. Create the directory in the correct layer/category.
2. Add a `CMakeLists.txt` following the local parent pattern.
3. Add `add_subdirectory()` in the parent `CMakeLists.txt`.
4. Link the new target from the nearest aggregator target.
5. Create placeholder files, verify the build, then implement.

### Library Patterns

- Use a real library target when the module has `.cc` files.
- Use an `INTERFACE` library only when the module is header-only.
- Prefer linking existing targets over duplicating include directories.
- When restructuring directories, update every affected `CMakeLists.txt`.

## Checklist

Before handoff, verify:

- [ ] New files are placed in the correct layer and category
- [ ] CMake targets follow the repository naming pattern
- [ ] Parent `CMakeLists.txt` files include the new subdirectory or target
- [ ] Dependencies are expressed through target links, not duplicated include paths
- [ ] Tests mirror the source layout where needed
- [ ] The affected build targets still compile

## Related Docs

- `../project-constraints.md`
- `database-guidelines.md`
- `quality-guidelines.md`
