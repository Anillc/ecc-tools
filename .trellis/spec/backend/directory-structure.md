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

### CTS Flow Framework

Keep CTS flow code aligned with the current CTS architecture:

```text
setup -> synthesis -> instantiation -> evaluation -> report
```

Use these placement boundaries:
- `api/`: stable external `CTSAPI` entry points only.
- `source/flow/`: root CTS lifecycle ownership. The root exposes only `Flow.hh`, `Flow.cc`, and build metadata; `Flow` owns flow state and delegates stage work to the subflow entries.
- `source/flow/setup/`: config readiness, work/log/statistics/visualization directories, schema/log initialization, and adapter initialization.
- `source/flow/synthesis/`: CTS algorithm orchestration. The root contains only `Synthesis.hh`, `Synthesis.cc`, and build metadata; helpers live under `distribution/`, `topology/`, `htree/`, or `trace/`.
- `source/flow/synthesis/topology/`: CTS topology formation boundary. The root exposes `Topology.hh/.cc`; sink-side branch code lives under `sink/`, source-trunk code under `trunk/`, and temporary buffer/net insertion under `buffer/`.
- `source/flow/synthesis/htree/`: H-tree topology implementation. The root exposes `HTree.hh/.cc`; internals live under `characterization/`, `pattern/`, and `embedding/`.
  - Keep `htree/characterization/` limited to characterization grid, library, and flow concepts. Topology-side H-tree option assembly belongs with the topology callers, pattern-search boundary constraints belong under `htree/pattern/`, and buffer port/object lookup tables belong under `htree/embedding/`.
- `source/flow/instantiation/`: materialization of synthesized CTS topology into committed design/iDB state, split into `design_conversion/DesignConversion` and `idb_conversion/IdbConversion` when needed.
- `source/flow/evaluation/`: readonly metric computation over committed CTS results. The root exposes `Evaluation.hh/.cc`; metric helpers live under responsibility subfolders.
- `source/flow/report/`: final output generation. The root exposes `Report.hh/.cc`; output responsibilities live under `summary/`, `statistics/`, `export/`, and `visualization/`.

Do not reintroduce top-level peer architecture directories under `source/flow/` named `run_setup`, `stage`, `htree`, `clock_tree_view`, `netlist`, or `visualization`.

### Placement

- Put public external entry points in `api/`.
- Put internal algorithm code in `source/module/`.
- Put shared data and adapters in `source/database/`.
- Put reusable helper code in `source/utils/`.
- Put tests under `test/`, mirroring the source layout as closely as practical.
- Keep `api/CTSAPI` focused on external entry points; internal CTS flow lifecycle steps belong under `source/flow`.
- Keep runtime config access at API/flow/database/test boundaries; code under `source/module/` should receive explicit options instead of reading `CONFIG_INST`.
- Put visualization-only format writers under `source/flow/report/visualization/`.
- Put stable shared routing database types under `source/database/routing/`; keep routing algorithms under `source/module/routing/`.

### CMake Target Naming

Use hierarchical target names:

```text
icts_{tier}_{category}_{module}
```

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
- [ ] Flow changes preserve `setup -> synthesis -> instantiation -> evaluation -> report`
- [ ] The affected build targets still compile

## Related Docs

- `../project-constraints.md`
- `database-guidelines.md`
- `quality-guidelines.md`
