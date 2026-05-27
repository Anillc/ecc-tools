# Clean CTS Static Archive CMake Links Design

## Current Problem

CTS CMake still contains direct links to generated static archive files:

```cmake
${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/libicts_*.a
```

Those paths bypass CMake's target dependency model. They hide ownership problems, do not propagate include/link interfaces, and are brittle when targets are renamed, output directories change, or static link order shifts.

The initial scan found the remaining occurrences under H-tree synthesis and H-tree characterization. These are expected to be artifacts of prior attempts to work around static archive link order.

## Design Direction

Use target names in `target_link_libraries()` only:

- link H-tree state assembly to the real topology/plan/characterization targets it calls;
- link the H-tree facade to its direct dependencies through targets, not archive files;
- link characterization library to characterization and STA adapter targets by target name;
- adjust `PRIVATE` versus `PUBLIC` visibility according to whether headers expose the dependency;
- if target-only links reveal unresolved symbols, fix the local dependency ownership or split the target that owns the symbol.

## Dependency Cleanup Principles

- A CMake target should link the targets required by its own `.cc` files.
- Aggregator/facade targets may link subtargets, but should not use archive paths to compensate for missing direct dependencies in child targets.
- Prefer `PRIVATE` for implementation-only dependencies.
- Use `PUBLIC` only when a public header includes or exposes types from another target.
- Avoid creating object libraries or global link groups unless local target cleanup cannot express the dependency graph.

## Validation

After CMake cleanup:

1. Verify archive references are gone from CTS CMake.
2. Build focused CTS/H-tree targets.
3. Run focused H-tree tests.
4. Build `iEDA` to catch final link-order failures.
5. Run the final iCTS checker.

The implementation is intentionally left uncommitted for user review.
