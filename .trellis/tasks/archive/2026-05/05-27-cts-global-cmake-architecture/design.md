# CTS Global CMake Architecture Analysis

## Current Shape

The CTS CMake hierarchy is broad and mostly mirrors the source directory structure:

- `src/operation/iCTS/CMakeLists.txt` defines CTS-wide paths and high-level debug options, then adds `external_libs`, `source`, `api`, and `test`.
- `source/CMakeLists.txt` defines the source-layer categories:
  - `database`
  - `utils`
  - `module`
  - `flow`
- `source/flow` follows the intended CTS lifecycle:
  - `setup`
  - `synthesis`
  - `optimization`
  - `instantiation`
  - `evaluation`
  - `report`
- `source/module` owns reusable algorithm modules:
  - routing
  - timing
  - topology
  - characterization
  - analytical characterization
- `source/flow/synthesis` currently contains:
  - `distribution`
  - `trace`
  - `htree`
  - `topology`

Repository scan summary:

- 134 CTS `CMakeLists.txt` files total.
- 52 CMake files under `source/flow`.
- 19 CMake files under `source/module`.
- 13 CMake files under `source/database`.
- 42 CMake files under `test`.
- 123 `add_library()` declarations across all iCTS CMake files.
- 19 INTERFACE library targets, including aggregate or header-only targets such as `icts_source`, `icts_source_database`, `icts_source_module`, `icts_source_utils`, routing/timing/spatial database contracts, and topology config/kmeans/mcf contracts.
- 112 broad include-root exposures across source CMake files:
  - `${ICTS_SOURCE}`: 23 occurrences.
  - `${ICTS_FLOW}`: 53 occurrences.
  - `${ICTS_MODULE}`: 4 occurrences.
  - `${ICTS_DATABASE}`: 16 occurrences.
  - `${ICTS_UTILS}`: 16 occurrences.
- Aggregate target references in source CMake:
  - `icts_source_module`: only the module aggregator definition remains.
  - `icts_source_database`: 40 CMake files.
  - `icts_source_utils`: 66 CMake files.

The recently completed cleanup removed explicit generated static archive path links from CTS CMake. Current CTS CMake no longer matches:

```text
CMAKE_ARCHIVE_OUTPUT_DIRECTORY
libicts_*.a
```

## Strengths

The directory-to-target mapping is mostly understandable.

Each responsibility area usually owns a local target named with the hierarchical `icts_source_*` pattern. This makes it easy to find the CMake entry for a source directory, and it matches the current Trellis directory-structure spec.

The major source layers are recognizable.

`database`, `utils`, `module`, and `flow` are separate at the CMake level. This gives us a workable base for future boundary cleanup instead of needing a full rebuild of the source tree.

Most behavior directories already expose a facade target.

Examples include `icts_source_flow_synthesis_htree`, `icts_source_flow_synthesis_topology`, `icts_source_module_characterization`, `icts_source_module_routing`, and `icts_source_database_adapter_fast_sta`. This supports a staged refactor where subtargets can become private implementation details behind facade targets.

Fine-grained targets make local validation possible.

Focused targets such as `icts_source_flow_synthesis_htree_analytical_solver`, `icts_source_flow_synthesis_htree_solution`, and `icts_test_flow_synthesis_htree` allow incremental builds and targeted tests.

The immediate static-link hack has been removed.

The prior `*.a` workaround has been replaced with target links. The current baseline builds `iEDA`, passes focused H-tree tests, and passes the final iCTS checker with 0 in-scope findings.

## Weaknesses

The graph is still too easy to make order-sensitive.

Many small static libraries create a dense link graph. Even without explicit archive paths, static link order can become fragile when subtargets depend back into nearby peer targets. H-tree depth planning, topology pruning, synthesis state, topology synthesis, and trace/layout are the highest-risk cluster.

Aggregate INTERFACE targets can hide real dependencies.

`icts_source`, `icts_source_database`, `icts_source_module`, and `icts_source_utils` are convenient, but using them inside implementation targets can mask missing direct dependencies. The last cleanup already showed this: replacing broad `icts_source_module` exposed missing concrete links to routing, timing, characterization, and topology clustering.

Include roots are broader than ideal.

Many targets expose roots such as `${ICTS_SOURCE}`, `${ICTS_FLOW}`, `${ICTS_MODULE}`, or `${ICTS_DATABASE}`. This makes includes compile even when the target has not declared the precise dependency that owns the header. It also encourages short include forms that are sensitive to whichever target happens to be linked.

Some public headers expose heavy implementation dependencies.

Examples:

- `HTree.hh` includes module characterization contracts directly.
- `Topology.hh` includes H-tree and clustering contracts.
- Trace/layout headers consume topology and H-tree build structures.

Those public header includes force wider `PUBLIC` links and make target visibility cleanup harder.

Flow and algorithm ownership are partially mixed.

The spec says `module` owns algorithms and `flow` owns orchestration. In practice, H-tree, topology formation, analytical H-tree solver, depth planning, pruning, embedding, and solution selection all live under `source/flow/synthesis`. This may be valid for flow-specific synthesis algorithms, but the CMake graph has to compensate by linking many flow subtargets together.

The facade/implementation split is inconsistent.

Some directories have a clear root facade and private implementation slices. Others expose subfolder targets directly to peers. For example, `flow/synthesis` directly links `htree_characterization_library` and `trace_layout`, and `flow` directly links H-tree characterization library, which leaks synthesis details upward.

Debug option coverage is uneven.

Root CMake defines only a subset of debug options, while many lower targets check additional `DEBUG_ICTS_*` variables. Undefined variables default false, so this is not a build failure, but discoverability is poor.

Target boilerplate is high.

There are many repeated patterns:

- `add_library`
- `target_link_libraries`
- `target_include_directories`
- debug compile option blocks

This is manageable today, but it makes broad cleanup noisy and increases the chance of inconsistent visibility or include-root choices.

## Current Risk Areas

Highest-risk CMake areas:

- `source/flow/synthesis/htree`
- `source/flow/synthesis/topology`
- `source/flow/synthesis/trace`
- `source/flow/evaluation/qor`
- `source/module/characterization`
- `source/module/routing`
- `source/database/adapter/fast_sta`

These areas are risky because they cross CTS boundaries: synthesis state, route/layout data, characterization data, STA adapters, and reporting.

## Recommended Refactor Direction

Use a staged refactor, not a single global rewrite.

Stage 1: define target ownership rules.

- Internal implementation targets should link only concrete dependencies.
- Aggregate targets should be used mainly at package boundaries, API/test boundaries, or root executable-facing targets.
- Behavior directory facades should be the preferred external dependency.
- Subfolder implementation targets should become private to the nearest facade where possible.

Stage 2: reduce broad include roots.

- Keep `${ICTS_SOURCE}` only where headers intentionally use rooted CTS includes.
- Remove child-directory include roots that make private headers look public.
- Standardize include style per behavior directory facade.

Stage 3: shrink public headers.

- Forward declare where possible.
- Move implementation-only includes from headers to `.cc` files.
- Introduce narrow public DTO headers only when a public cross-target contract genuinely needs structured data.

Stage 4: clean the synthesis cluster.

- Keep `HTree`, `Topology`, and trace/layout as facade targets.
- Make depth plan, pruning, embedding, solution slices, and trace helpers private implementation targets behind facades where possible.
- Avoid peer-to-peer cycles among H-tree, topology, and trace by extracting small stable contracts only when needed.

Stage 5: clean root aggregation.

- Reevaluate `icts_source`, `icts_source_module`, `icts_source_database`, and `icts_source_utils`.
- Preserve aggregate targets where useful, but prevent internal implementation targets from depending on them by default.

## Remediation Boundaries

Allowed changes:

- CMake target dependencies and visibility.
- Include forms that need to match narrowed target include roots.
- Header forward declarations and moving implementation-only includes into `.cc` files.
- Small stable contract headers when they reduce public dependency width and match CTS terminology.
- Debug option registration cleanup when it is directly tied to touched CMake targets.

Avoided changes:

- Algorithm or runtime behavior changes.
- User-facing CTS config changes.
- Changes outside `src/operation/iCTS` unless a build failure proves an external interface must be touched.
- Spec changes unless the work discovers a durable convention that belongs in global guidelines.

## Cleanup Strategy

The cleanup should converge from high-risk clusters outward:

1. Synthesis cluster public contract cleanup.
2. Synthesis cluster CMake ownership cleanup.
3. Module/database concrete target cleanup.
4. Include-root tightening.
5. Root aggregation/debug-option cleanup.

Each phase should preserve a buildable state before moving to the next phase. When a phase reveals a circular dependency, prefer one of these fixes in order:

1. Move implementation-only includes from headers to `.cc`.
2. Extract a narrow CTS-domain contract header.
3. Move a subtarget behind the nearest facade.
4. Split a target by responsibility.
5. Use a local target-based link group only if the structure cannot be expressed cleanly and document the tradeoff.

## Validation Strategy For Future Implementation

Every implementation slice should run:

```bash
rg -n "CMAKE_ARCHIVE_OUTPUT_DIRECTORY|\\.a\\b|libicts_.*\\.a" src/operation/iCTS -g 'CMakeLists.txt'
cmake --build build --target iEDA -- -j 16
cmake --build build --target icts_source_flow_synthesis_htree icts_source_flow_synthesis_topology icts_source_flow_synthesis_trace -- -j 16
ctest --test-dir build -R '^icts_test_flow_synthesis_htree$|^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

If the slice touches test CMake or API-facing targets, extend validation to the relevant CTS tests and API target.

Final validation order is strict:

1. Build and focused tests.
2. Real design validation:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

3. Only after the real design passes, run:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Initial Recommendation

The first implementation slice should not start from root aggregation. It should start from the synthesis cluster because that is where the previous link-order issue happened and where the flow/module boundary is currently most tangled.

Recommended first slice:

1. Audit `flow/synthesis/htree`, `flow/synthesis/topology`, and `flow/synthesis/trace` public headers.
2. Identify which subtargets are real external contracts and which are implementation details.
3. Reduce public include exposure and convert peer links to facade links where it does not create cycles.
4. Validate with `iEDA`, focused H-tree tests, and full iCTS checker.
