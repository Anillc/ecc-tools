# Directory Structure

> How backend code is organized in the iCTS module.

---

## Overview

The iCTS module (`src/operation/iCTS/`) follows a **three-tier architecture**: API, Source, and Test. This pattern is standard across iEDA operation modules.

---

## Top-Level Layout

```
src/operation/iCTS/
├── CMakeLists.txt              # Top-level: sets paths, includes all tiers
├── README.md
├── api/                        # TIER 1: Public API (entry point)
│   ├── CMakeLists.txt
│   ├── CTSAPI.cc
│   └── CTSAPI.hh
├── source/                     # TIER 2: Implementation (all internal logic)
│   ├── CMakeLists.txt          # Aggregates sub-categories
│   ├── database/               # Data/model + adapter layer
│   │   ├── adapter/            # External subsystem adapters colocated with DB-facing integration
│   │   │   └── sta/            # iSTA adapter for iCTS internal use
│   │   ├── config/             # Configuration (Config singleton)
│   │   ├── design/             # Core objects: Clock, Inst, Net, Pin, Design
│   │   ├── io/                 # iDB adapter (Wrapper singleton)
│   │   ├── spatial/            # Geometric types: Point, Rect, Region, Tree
│   │   ├── routing/            # Routing DB types: terminals, Steiner tree
│   │   ├── timing/             # Timing DB types: RCTree
│   │   └── characterization/   # Electrical characterization data
│   ├── utils/                  # Utility layer
│   │   ├── logger/             # Logger singleton + CTS_LOG_* macros
│   │   └── geometry/           # Shared geometry helpers
│   ├── module/                 # Algorithm modules
│   │   ├── topology/           # [ENABLED] Tree topology generation
│   │   │   ├── clustering/     # Bi-partition clustering
│   │   │   ├── kmeans/         # K-means clustering
│   │   │   └── mcf/            # Min-cost flow
│   │   ├── characterization/   # [ENABLED] STA-based characterization
│   │   │   ├── CharBuilder.*   # Characterization builder (main entry)
│   │   │   ├── SegmentCharTable.hh   # Segment char lookup table
│   │   │   ├── HTreeTopologyCharTable.hh # H-tree char lookup table
│   │   │   ├── HashJoinEngine.hh     # Hash-join for pattern composition
│   │   │   ├── PatternCombiner.hh    # Pattern combination logic
│   │   │   ├── Pruner.hh            # Pattern pruning
│   │   │   ├── SegmentTraits.hh     # Segment-specific traits
│   │   │   └── HTreeTraits.hh      # H-tree-specific traits
│   │   ├── routing/            # [ENABLED] Clock tree routing facade + legalization
│   │   │   ├── local_legalization/ # Standalone point-based legalization
│   │   │   ├── flute/          # FLUTE routing
│   │   │   ├── salt/           # SALT routing
│   │   │   ├── bound_skew_tree/ # Bound Skew Tree
│   │   │   └── concurrent_bst_salt/ # Concurrent BST + SALT
│   │   ├── timing/             # [ENABLED] RCTree timing propagation
│   │   ├── buffering/          # [DISABLED] Buffer insertion
│   │   ├── drv/                # [DISABLED] Design rule violation fixing
│   │   ├── optimization/       # [DISABLED] Post-CTS optimization
│   │   └── report/             # [DISABLED] Report generation
│   └── flow/                   # Flow orchestration
├── test/                       # TIER 3: Tests
│   ├── CMakeLists.txt          # Single executable linking all tests
│   ├── main.cc                 # Test entry point
│   ├── common/                 # Test utilities
│   ├── module/                 # Module-level tests
│   │   ├── topology/
│   │   │   ├── clustering/
│   │   │   ├── kmeans/
│   │   │   └── mcf/
│   │   ├── characterization/
│   │   ├── routing/
│   │   ├── buffering/
│   │   ├── drv/
│   │   ├── optimization/
│   │   ├── report/
│   │   └── timing/
│   ├── database/               # Database tests
│   │   ├── config/
│   │   ├── design/
│   │   ├── io/
│   │   └── spatial/
│   ├── flow/                   # Flow tests
│   └── utils/                  # Utility tests
│       ├── geometry/
│       └── logger/
└── external_libs/              # External dependency declarations
    ├── CMakeLists.txt
    ├── icts_api_external_libs.cmake
    ├── icts_source_external_libs.cmake
    └── icts_test_external_libs.cmake
```

---

## Three-Tier Architecture

| Tier | Directory | CMake Target | Role |
|------|-----------|--------------|------|
| **API** | `api/` | `icts_api` | Public interface for external callers; accessed via `CTSAPIInst` |
| **Source** | `source/` | `icts_source` | All internal implementation, composed of sub-targets |
| **Test** | `test/` | `icts_test` (executable) | Links `icts_source` + `icts_api` |

**Dependency direction**: API → Source ← Test (API and Test both depend on Source; Source never depends on API)

---

## Source Sub-Categories

| Category | Directory | CMake Target | Purpose |
|----------|-----------|--------------|---------|
| database | `source/database/` | `icts_source_database` | Data model, DB-facing adapters, config, IO, routing/timing/spatial data |
| utils | `source/utils/` | `icts_source_utils` | Logging, geometry helpers |
| module | `source/module/` | `icts_source_module` | CTS algorithm implementations |
| flow | `source/flow/` | `icts_source_flow` | Flow orchestration (present but currently not linked) |

### Module Enable/Disable Status

Modules are enabled/disabled by including/excluding `add_subdirectory()` in `source/module/CMakeLists.txt`:

| Module | Status | Purpose |
|--------|--------|---------|
| `topology` | **Enabled** | Tree topology generation (clustering, kmeans, mcf) |
| `characterization` | **Enabled** | STA-based characterization (CharBuilder, hash-join engine) |
| `routing` | **Enabled** | Clock tree routing (FLUTE, SALT, BST, CBS) |
| `timing` | **Enabled** | RCTree timing propagation |
| `buffering` | Disabled | Buffer insertion |
| `drv` | Disabled | Design rule violation fixing |
| `optimization` | Disabled | Post-CTS optimization |
| `report` | Disabled | Report generation |

---

## CMake Target Naming Convention

Targets follow a hierarchical underscore pattern:
```
icts_{tier}_{category}_{module}
```

Examples:
- `icts_api`
- `icts_source_database_adapter_sta`
- `icts_source_database_config`
- `icts_source_module_topology`
- `icts_source_utils_logger`

---

## Adding New Files to an Existing Module

1. Create the `.hh`/`.cc` files with copyright header
2. Add the `.cc` file to the module's `CMakeLists.txt` `add_library()` call
3. Compile to verify

## Adding a New Module

1. Create the module directory under the appropriate category
2. Create `CMakeLists.txt` following the patterns below
3. Add `add_subdirectory()` in the parent CMakeLists.txt
4. Link the new target in the parent's aggregator library
5. Create placeholder header, compile, then implement

### Pattern A: Real library (has `.cc` files)

```cmake
add_library(icts_source_module_newmod
    ${PATH}/NewMod.cc
)
target_include_directories(icts_source_module_newmod PUBLIC ${PATH})
target_link_libraries(icts_source_module_newmod PRIVATE <dependencies>)
```

### Pattern B: Header-only (INTERFACE library)

```cmake
add_library(icts_source_database_newdata INTERFACE)
target_include_directories(icts_source_database_newdata INTERFACE ${PATH})
target_link_libraries(icts_source_database_newdata INTERFACE <dependencies>)
```

---

## File Naming Conventions

- **PascalCase** for all `.cc` and `.hh` files: `TopologyGen.hh`, `Clock.hh`
- **Acronyms** stay all-caps: `CTSAPI.hh`, `FLUTE.cc`, `CBS.cc`
- **Test files** append `Test`: `TopologyGenTest.cc`, `CharacterizationTest.cc`
- **One class per file** (header + optional source): `Clock.hh` contains `class Clock`

---

## Examples of Well-Organized Modules

- **`source/database/design/`** — Clean header-only data model (Clock, Inst, Net, Pin)
- **`source/database/adapter/sta/`** — Dedicated iSTA adapter used by routing / timing / characterization internals
- **`source/utils/logger/`** — Logger singleton with macros
- **`source/module/topology/`** — Algorithm module with sub-modules (clustering, kmeans, mcf)
- **`source/database/characterization/`** — Characterization types (CharCore, SegmentChar, BufferingPattern, etc.)
