# iCTS Test Layout

The test tree mirrors `source/` and keeps reusable helpers in `test/common/`.
Support code is split into semantic folders with local `CMakeLists.txt` files
so feature-specific tests can link only the pieces they need.

## Common Support

- `test/common/types/`: shared POD-style test data types
- `test/common/data/`: synthetic load and point generation
- `test/common/io/`: artifact-path resolution, report emission, text-log output
- `test/common/topology/`: tree and cluster analysis helpers
- `test/common/visualization/`: SVG writers
- `test/common/logging/`: scoped logging redirection helpers
- `test/common/clustering/metrics/`: cluster geometry and metrics helpers
- `test/common/clustering/artifact/`: cluster-to-artifact materialization helpers
- `test/common/realtech/asset/`: repo-local ICS55 workspace probing and environment bootstrap
- `test/common/realtech/load/`: real-design load extraction and synthetic fallback generation
- `test/common/realtech/support/`: cached real-tech setup state and shared load-selection helpers

## Module Tests

- `test/database/spatial/`: spatial database regressions
- `test/module/characterization/`: split translation units for segment join, H-tree join, pruner, and pattern hash, plus header-only `support/`
- `test/module/routing/`: routing and legalization regressions
- `test/module/topology/topology_gen/`: topology-generation regression, with support split into `support/{case,analysis,artifact,core}`
- `test/module/topology/fast_clustering/synthetic/`: synthetic fast-clustering regressions
- `test/module/topology/fast_clustering/realtech/`: fast-only real-tech clustering benchmark

## Executable Targets

- `icts_test_database_spatial`
- `icts_test_module_characterization`
- `icts_test_module_routing`
- `icts_test_module_topology_gen`
- `icts_test_module_topology_fast_clustering`
- `icts_test_module_topology_fast_clustering_realtech_benchmark`

## Support Targets

- `icts_test_common_*`: reusable test helpers grouped by common capability
- `icts_test_module_topology_gen_*`: topology-gen support targets grouped by case generation, analysis, artifact emission, and scenario orchestration

The real-tech test helpers probe repo-local ICS55 workspaces under
`scripts/design/` and resolve the ICS55 PDK root from
`ICTS_REALTECH_PDK_DIR`, `PDK_DIR`, or the checked-in run scripts. If the
required LEF/DEF/LIB/SDC files are available, the tests load real tech/design
state once and run on real design loads. Otherwise they fall back to synthetic
loads while keeping the same report pipeline.

## Output

Set `ICTS_TEST_OUTPUT_DIR` to control where SVGs and logs are written. The
default is `icts_test_output` next to the running test executable. For binaries
under `bin/`, artifacts are written under `bin/icts_test_output/`.

Each executed gtest case now also gets a dedicated root under:

- `icts_test_output/gtest/<executable>/<test_suite>/<test_name>/`

That per-test root always contains:

- `cts.log`: schema-driven structured test log
- `test.log`: captured stdout/stderr for the individual test case

- topology generation: `icts_test_output/topology_gen/`
- clustering and real-tech clustering benchmark: `icts_test_output/clustering/`
