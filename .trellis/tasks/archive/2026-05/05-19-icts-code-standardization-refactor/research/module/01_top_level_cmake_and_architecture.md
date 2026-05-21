# Module Layer Top-Level CMake & Architecture

- **Query**: 顶层结构、CMake 组织、Module 基类/注册机制
- **Scope**: internal
- **Date**: 2026-05-19

## Module Layer Layout (factual)

`src/operation/iCTS/source/module/` directly contains 5 sub-modules + a top-level
`CMakeLists.txt`:

```
module/
├── CMakeLists.txt          (33 lines, INTERFACE aggregator)
├── analytical_characterization/   3 .cc, 3 .hh, single CMake target
├── characterization/              13 .cc, 7 .hh, single CMake target (flat)
├── routing/                       8 subdirs, multi-target
├── timing/                        1 .cc, 1 .hh, single CMake target
└── topology/                      6 subdirs, multi-target
```

## Top-Level CMake (`module/CMakeLists.txt`)

- Defines path variables `ICTS_MODULE_ROUTING/TIMING/TOPOLOGY/CHARACTERIZATION/ANALYTICAL_CHARACTERIZATION`.
- `add_subdirectory` invokes the five sub-modules in this order:
  characterization → routing → timing → topology → analytical_characterization.
- Creates **one INTERFACE library** named `icts_source_module` that simply
  `target_link_libraries` against:
  - `icts_source_database`
  - `icts_source_module_routing`
  - `icts_source_module_timing`
  - `icts_source_module_topology`
  - `icts_source_module_characterization`
  
  Note: `icts_source_module_analytical_characterization` is **not linked into
  the aggregator** even though its subdirectory is added — flow layer must
  link it explicitly.
- Exposes `${ICTS_MODULE}` as a public include directory.

## Granularity of CMake Targets

The granularity is inconsistent — there is no single rule:

| Sub-module                  | Sub-targets                                                                                                                                                       | Style                |
|-----------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------|
| `analytical_characterization` | 1 target: `icts_source_module_analytical_characterization`                                                                                                       | flat                 |
| `characterization`            | 1 target: `icts_source_module_characterization` (12 .cc files in one library)                                                                                    | flat                 |
| `timing`                      | 1 target: `icts_source_module_timing`                                                                                                                            | flat                 |
| `topology`                    | 6 targets: `_topology`, `_topology_config` (INTERFACE), `_topology_clustering`, `_topology_cluster_constraints`, `_topology_fast_clustering`, `_topology_kmeans` (INTERFACE), `_topology_mcf` (INTERFACE) | nested + INTERFACE   |
| `routing`                     | 8 targets: `_routing`, `_routing_database` (INTERFACE), `_routing_helper`, `_routing_flute`, `_routing_salt`, `_routing_bst`, `_routing_cbs`, `_routing_local_legalization` | nested fine-grained  |

INTERFACE targets are used for header-only or path-only sub-modules
(`config/`, `kmeans/`, `mcf/`, `routing/database/`).

## Module Base Class / Registry / Factory

**None found.** Searches for `class Module`, registry, or factory patterns
inside `module/` return nothing. There is no:

- common `Module` base class with `init/run/finalize` lifecycle
- registration mechanism (no `static bool registered = …` pattern)
- factory dispatching by name or enum

Each sub-module exposes a different surface, listed in
`02_entry_styles_inconsistency.md`.

## CMake Library Style by File

| File                                                                                          | Style                                          |
|-----------------------------------------------------------------------------------------------|------------------------------------------------|
| `module/CMakeLists.txt:1-33`                                                                  | INTERFACE aggregator                           |
| `module/analytical_characterization/CMakeLists.txt:7`                                         | Plain `add_library` with explicit src list     |
| `module/characterization/CMakeLists.txt:16`                                                   | Plain `add_library` with explicit src list     |
| `module/timing/CMakeLists.txt:1`                                                              | Plain `add_library` with inline src            |
| `module/topology/CMakeLists.txt:1-25`                                                         | Hybrid: aggregator + plain target              |
| `module/topology/config/CMakeLists.txt:1-8`                                                   | INTERFACE                                       |
| `module/topology/kmeans/CMakeLists.txt:1-23`                                                  | INTERFACE                                       |
| `module/topology/mcf/CMakeLists.txt:1-24`                                                     | INTERFACE                                       |
| `module/topology/fast_clustering/CMakeLists.txt:1-43`                                         | Plain `add_library` with explicit src list     |
| `module/topology/cluster_constraints/CMakeLists.txt:1-27`                                     | Plain `add_library` with single src            |
| `module/topology/clustering/CMakeLists.txt:1-34`                                              | Plain `add_library` with single src            |
| `module/routing/CMakeLists.txt:1-9`                                                           | Pure aggregator (only `add_subdirectory`)      |
| `module/routing/database/CMakeLists.txt:1-13`                                                 | INTERFACE                                       |
| `module/routing/router/CMakeLists.txt:1-40`                                                   | Plain `add_library` with single src            |
| `module/routing/helper/CMakeLists.txt:1-31`                                                   | Plain `add_library` with 2 srcs                |
| `module/routing/bound_skew_tree/CMakeLists.txt:1-47`                                          | Plain `add_library` with 15 srcs               |
| `module/routing/concurrent_bst_salt/CMakeLists.txt:1-30`                                      | Plain `add_library` with 1 src                 |
| `module/routing/flute/CMakeLists.txt:1-31`                                                    | Plain `add_library` with 1 src                 |
| `module/routing/salt/CMakeLists.txt:1-30`                                                     | Plain `add_library` with 1 src                 |
| `module/routing/local_legalization/CMakeLists.txt:1-25`                                       | Plain `add_library` with 1 src                 |

## Caveats / Not Found

- No `analytical_characterization` target in the top-level aggregator
  `icts_source_module`. The flow layer (or downstream caller) must link it
  explicitly. This is asymmetric with the other 4 sub-modules.
- The `module/routing/database` directory does not contain its own headers —
  its CMake `INTERFACE` target simply re-exports `${ICTS_DATABASE_ROUTING}`
  (from `database/routing`). This makes `module/routing/database/` an empty
  shell whose only purpose is `target_include_directories(... INTERFACE
  ${ICTS_DATABASE_ROUTING})`. The directory exists in `module/` but contains
  no source files.
