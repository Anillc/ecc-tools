# Design

## Problem

The previous task fixed `ecc_bin` static archive order sensitivity, but the broader iCTS CMake graph still has structural debt:

- some source files are represented by stale or orphan targets;
- some targets compile because an aggregate happens to bring in providers, not because the target declares its own dependencies;
- `SourceTrunk` uses a type and formatter owned by the parent `Topology` target, creating a child-to-parent dependency shape;
- one test target still uses a CMake link group rescan fallback;
- flow CMake traversal order was adjusted for link order and should be restored to the domain lifecycle once direct dependencies are correct.

The design goal is to make the target graph express ownership and symbol dependencies directly.

## Architecture Decisions

### 1. Instantiation / iDB Conversion

Keep `IdbConversion.cc` owned by `icts_source_flow_instantiation` because it is an implementation detail of the instantiation stage.

Remove the orphan `source/flow/instantiation/idb_conversion/CMakeLists.txt`. Do not keep a stale target for the same source file.

If a future task needs `IdbConversion` as an independent library, it should be reintroduced as either:

- an `OBJECT` library consumed by `icts_source_flow_instantiation`, or
- a real stage target with a clear external consumer and verified archive order.

### 2. Missing Direct Dependencies

Add direct target dependencies where object symbols show direct usage:

- `icts_source_flow_synthesis_realization`
  - move the shared clock-tree realization helper from the old instantiation conversion location into `flow/synthesis/realization`;
  - build as an `OBJECT` target because both distribution and topology consume the same implementation;
  - require consuming targets to declare `icts_source_database_io` and other implementation providers directly.
- `icts_source_flow_synthesis_topology_trunk`
  - add `icts_source_flow_synthesis_topology_buffer`.
- `icts_source_flow_synthesis_trace_distance`
  - add `icts_source_flow_synthesis_topology_buffer`.

This keeps the local target closure correct instead of relying on parent aggregators.

The normal static-library form for `ClockTreeRealization` was rejected during implementation because the same static archive was consumed by both
`icts_source_flow_synthesis_distribution` and `icts_source_flow_synthesis_topology`. CMake could only satisfy one-pass static linking by repeating
the provider archive. The object-library form keeps a single source owner and lets each consuming archive carry the implementation object without
`LINK_GROUP`, linker groups, or intentional duplicate archive order.

The helper belongs under synthesis because it realizes synthesized clock-tree objects into the internal CTS `Design` graph. It is not a generic
database model type and is not the iDB writeback conversion owned by instantiation.

### 3. SourceTrunkStage Ownership

Move `Topology::SourceTrunkStage` out of `Topology` into a neutral topology contract that both parent and child can depend on.

Preferred shape:

- create `source/flow/synthesis/topology/SourceTrunkStage.hh/.cc`;
- define `enum class SourceTrunkStage` and `ToString(SourceTrunkStage)`;
- add a small target `icts_source_flow_synthesis_topology_source_trunk_stage`;
- link both `icts_source_flow_synthesis_topology` and `icts_source_flow_synthesis_topology_trunk` to that target.

This removes `topology_trunk -> topology` symbol dependency.

### 4. Test Link Group Removal

The synthetic fast-clustering topology test currently uses:

```cmake
$<LINK_GROUP:RESCAN,...>
```

Remove the link group and express the real test dependencies. If the test exposes another static archive cycle, treat it as a normal missing dependency or target-boundary problem.

### 5. Flow Order

Restore source flow `add_subdirectory()` order to:

```text
setup -> synthesis -> optimization -> instantiation -> evaluation -> report
```

The final link order must be controlled by direct dependencies, not by directory traversal side effects.

### 6. CMake Style Cleanup

Keep cleanup scoped:

- delete stale CMake files;
- use multi-line target declarations for touched CMake;
- avoid broad new include paths;
- do not attempt to fully split `fast_sta/CMakeLists.txt` unless needed for correctness in this task.

The fast STA CMake file remains a known future cleanup candidate, but this task focuses on correctness and visible architecture debt from the audit.

## Validation Design

Use these checks after implementation:

1. Configure/build `ecc_bin`.
2. Relink using the generated `ecc_bin` command after removing duplicate `libicts_source*.a` entries.
3. Run the required iCTS design script:
   `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
4. Run full iCTS checker:
   `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`

## Risks

- Moving `SourceTrunkStage` changes public type names from `Topology::SourceTrunkStage` to `SourceTrunkStage`. Keep the edit mechanical and limited to iCTS.
- Removing the test link group may reveal older test-only dependency issues. Fix the dependency graph rather than restoring the fallback.
- Restoring flow order may expose unresolved symbols if any dependency is still implicit. Treat failures as missing target edges.
- Object libraries can hide architecture mistakes if used broadly. Keep `icts_source_flow_synthesis_realization` narrow and do not use object
  targets as a general substitute for normal CMake dependencies.
