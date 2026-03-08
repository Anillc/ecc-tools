# Build iCTS routing module

## Goal
Migrate the legacy routing implementation from `src/operation/iCTS_bak/source/solver/tools/tree_builder` and its direct dependencies into `src/operation/iCTS/source/module/routing`, preserving algorithm logic as much as possible in the first stage.

## Requirements
- Build the routing module under `src/operation/iCTS/source/module/routing`
- Keep these submodules:
  - `bound_skew_tree`
  - `concurrent_bst_salt`
  - `flute`
  - `salt`
  - `local_legalization`
  - `timing`
  - `legacy_module`
- `bound_skew_tree` implements BST and should not expose `bst` namespace externally
- `concurrent_bst_salt` implements CBS with core class `CBSRouter`
- `flute` implements `FLUTERouter`
- `salt` implements `SALTRouter`
- `timing` keeps only wire-related timing calculations: elmore, slew, wirelength, cap
- Remove cell-related timing logic from the new timing module
- `legacy_module` temporarily hosts old data structures and compatibility code needed to keep the copied routing logic running
- `src/operation/iCTS/source/module/routing/Router.hh` is the main external entry point
- First-stage principle: directly copy the four routing algorithms where possible; if a dependency is missing, place it into `legacy_module`
- First-stage Router interface may use legacy routing types externally

## Acceptance Criteria
- [x] `icts_source_module_routing` is defined in routing CMake
- [x] routing module is enabled from `src/operation/iCTS/source/module/CMakeLists.txt`
- [x] FLUTE / SALT / BST / CBS code exists in the requested subdirectories
- [x] `Router.hh` provides a unified external dispatch entry
- [ ] public BST-facing headers no longer expose `bst` namespace
- [ ] timing module excludes cell-delay / cell-sizing logic and no longer exposes cell-related compatibility APIs
- [x] migrated code builds successfully

## Current Progress (2026-03-08)
### Completed
- Built and enabled `src/operation/iCTS/source/module/routing` as `icts_source_module_routing`
- Migrated stage-1 routing submodules:
  - `bound_skew_tree`
  - `concurrent_bst_salt`
  - `flute`
  - `salt`
  - `local_legalization`
  - `timing`
  - `legacy_module`
- Added public stage-1 routing facades / entry points:
  - `Router`
  - `FLUTERouter`
  - `SALTRouter`
  - `BSTRouter`
  - `CBSRouter`
- Integrated routing target into `src/operation/iCTS/source/module/CMakeLists.txt`
- Completed stage-1 wrapper / CTSAPI / CMake cleanup needed to support routing build
- Verified focused builds for CTS targets including routing

### Remaining
- `bound_skew_tree` public headers still expose `namespace bst`; this still needs to be hidden/removed at the public interface boundary
- `timing/TimingPropagator` is mostly wire-oriented now, but still retains stage-1 compatibility traces such as `initLoadPinDelay(...)` and `cell_master`-related handling; this needs one more cleanup pass to fully match the spec

## Technical Notes
- Reuse old code from `iCTS_bak` as directly as possible
- Prefer local compatibility wrappers over invasive algorithm rewrites
- Follow iCTS backend spec: `.hh/.cc`, `CTS_LOG_*`, PascalCase file/class names, `icts` namespace
- Do not commit changes