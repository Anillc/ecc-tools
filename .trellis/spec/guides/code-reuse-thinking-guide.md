# Code Reuse Thinking Guide

> **Purpose**: Stop and think before creating new code -- does it already exist?

---

## The Problem

**Duplicated code is the #1 source of inconsistency bugs.**

When you copy-paste or rewrite existing logic:
- Bug fixes don't propagate
- Behavior diverges over time
- Codebase becomes harder to understand

In iCTS, common duplication targets include geometric calculations, singleton access patterns, config value lookups, and CMake target wiring.

---

## Before Writing New Code

### Step 1: Search First

```bash
# Search for similar function names in iCTS
grep -r "functionName" src/operation/iCTS/

# Search for similar geometric logic in utils and spatial
grep -r "keyword" src/operation/iCTS/source/utils/ src/operation/iCTS/source/database/spatial/

# Search for similar singleton access patterns
grep -r "CONFIG_INST\.\|WRAPPER_INST\.\|STA_ADAPTER_INST\." src/operation/iCTS/
```

### Step 2: Ask These Questions

| Question | If Yes... |
|----------|-----------|
| Does a similar function exist in `utils/geometry/`? | Use or extend it |
| Is this singleton access pattern used elsewhere? | Follow the existing pattern |
| Could this be a shared utility in `utils/` or `database/spatial/`? | Create it in the right place |
| Am I copying code from another module? | **STOP** -- extract to shared |
| Does a CMake INTERFACE target already provide this header? | Link it instead of duplicating include paths |

---

## Common Duplication Patterns in iCTS

### Pattern 1: Geometric Calculations

**Bad**: Reimplementing Manhattan distance or bounding-box overlap in a new module.

```cpp
// In module/routing/new_router/NewRouter.cc -- duplicated logic
int manhattan = std::abs(a.get_x() - b.get_x()) + std::abs(a.get_y() - b.get_y());
```

**Good**: Use existing utilities. `database/spatial/` provides `Point`, `Rect`, `Region`. `module/routing/bound_skew_tree/GeomCalc` provides line/box/distance operations. `utils/geometry/` provides shared header-only helpers.

```cpp
#include "GeomCalc.hh"  // existing geometry primitives
auto dist = icts::bst::GeomCalc::distance(point_a, point_b);
```

### Pattern 2: Singleton Access with Validation

**Bad**: Every module independently validates the same singleton state.

```cpp
// File A
auto* idb = WRAPPER_INST.get_idb();
if (idb == nullptr) {
  CTS_LOG_ERROR << "iDB is null";
  return;
}

// File B -- same pattern duplicated
auto* idb = WRAPPER_INST.get_idb();
if (idb == nullptr) {
  CTS_LOG_ERROR << "iDB is null";
  return;
}
```

**Good**: Validate once at the initialization boundary (e.g., in `CTSAPI::init()` or `Wrapper::init()`), then trust the invariant in downstream code. Use `CTS_LOG_FATAL_IF` for truly required preconditions at the entry point.

```cpp
// In CTSAPI::init() -- validated once
CTS_LOG_FATAL_IF(idb_builder == nullptr) << "iDB builder is null";
WRAPPER_INST.init(idb_builder);

// In module code -- trust the invariant, no redundant check
auto db_unit = WRAPPER_INST.queryDbUnit();
```

### Pattern 3: Config Value Lookup Patterns

**Bad**: Multiple modules hardcode the same fallback logic for config values.

```cpp
// In module A
auto layers = CONFIG_INST.get_routing_layers();
int layer = layers.empty() ? 1 : static_cast<int>(layers.front());

// In module B -- same fallback duplicated
auto layers = CONFIG_INST.get_routing_layers();
int layer = layers.empty() ? 1 : static_cast<int>(layers.front());
```

**Good**: Extract a named helper that encapsulates the resolution policy. See `Router.cc` for the `ResolveRoutingLayer` pattern -- it reads config with a fallback and can be called from multiple places.

```cpp
// Single resolution function in one location
auto ResolveRoutingLayer(const Options& options) -> int {
  if (options.routing_layer.has_value()) return options.routing_layer.value();
  const auto& layers = CONFIG_INST.get_routing_layers();
  return layers.empty() ? 1 : static_cast<int>(layers.front());
}
```

### Pattern 4: Algorithm Strategy Selection

**Bad**: Copy-pasting an entire routing algorithm to make a small variation.

**Good**: Parameterize the variation. iCTS already uses this pattern: `Router` is a facade that dispatches to `FLUTERouter`, `SALTRouter`, `BSTRouter`, or `CBSRouter` depending on the call. New routing strategies should follow the same dispatch pattern rather than duplicating the RCTree conversion and legalization logic.

### Pattern 5: CMake Target Duplication

**Bad**: Adding the same `target_include_directories` path to multiple CMakeLists.txt files.

**Good**: Use an INTERFACE library to express the dependency once, then link to it.

```cmake
# Define once in database/spatial/CMakeLists.txt
add_library(icts_source_database_spatial INTERFACE)
target_include_directories(icts_source_database_spatial INTERFACE ${ICTS_SPATIAL})

# Use everywhere via linking
target_link_libraries(icts_source_module_topology PRIVATE icts_source_database_spatial)
target_link_libraries(icts_source_module_routing  PRIVATE icts_source_database_spatial)
```

---

## Where Shared Code Lives

| Need | Look In | CMake Target |
|------|---------|--------------|
| Point, Rect, Region types | `database/spatial/` | `icts_source_database_spatial` |
| Tree data structure | `database/spatial/Tree.hh` | `icts_source_database_spatial` |
| SteinerTree, RCTree | `database/routing/` | `icts_source_database_routing` |
| Geometry helpers | `utils/geometry/` | `icts_source_utils_geometry` (INTERFACE) |
| BST geometry primitives | `module/routing/bound_skew_tree/GeomCalc` | `icts_source_module_routing_bst` |
| Logging macros | `utils/logger/Logger.hh` | `icts_source_utils_logger` |
| Config values | `database/config/Config.hh` | `icts_source_database_config` |
| Clock/Net/Pin/Inst model | `database/design/` | `icts_source_database_design` |

---

## When to Abstract

**Abstract when**:
- Same code appears 3+ times across different modules
- Logic is complex enough to have bugs (e.g., unit conversion, coordinate transforms)
- Multiple algorithms need the same data preparation or result conversion

**Don't abstract when**:
- Only used in one module
- Trivial one-liner that is clear in context
- Abstraction would require pulling in heavy dependencies for marginal gain

---

## After Batch Modifications

When you have made similar changes to multiple files:

1. **Review**: Did you catch all instances?
2. **Search**: Run grep across `src/operation/iCTS/` to find any missed
3. **Consider**: Should this be extracted to a shared location?

---

## Gotcha: Asymmetric Build Paths

**Problem**: When CMake aggregator targets (`icts_source`) link sub-targets, and a separate listing (e.g., test CMakeLists.txt) manually enumerates source files, adding a new file to the library but forgetting the test list (or vice versa) causes silent drift.

**Symptom**: Module builds and passes tests, but a new file is unreachable from tests, or test coverage quietly drops.

**Prevention checklist**:
- [ ] When adding a new `.cc` file, update both the module CMakeLists.txt and the test CMakeLists.txt if the file needs test coverage
- [ ] When restructuring directories, search for ALL CMakeLists.txt that reference the old path
- [ ] Verify that both `icts_source` and `icts_test` still build after the change

---

## Checklist Before Commit

- [ ] Searched `utils/`, `database/spatial/`, and existing modules for similar code
- [ ] No copy-pasted logic that should be shared
- [ ] Constants and config fallback values defined in one place
- [ ] CMake dependencies expressed through target linking, not duplicated include paths
- [ ] Similar patterns across modules follow the same structure
