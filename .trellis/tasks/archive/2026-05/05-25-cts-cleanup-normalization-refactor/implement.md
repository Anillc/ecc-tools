# CTS Cleanup And Normalization Refactor Implementation Summary

`ecc_dev_tools` was intentionally skipped during the initial implementation loop. After the user requested pre-commit validation, the full
`src/operation/iCTS` checker was run and passed before commit.

## Completed Changes

- FastSTA facade cleanup:
  - removed unused/test-only public APIs: `FastStaCharSegmentSpec`, `registerClockContext`, `rebuildClockContext`, single-edit
    `changeBufferMaster`, no-count `injectNetRouteTree`, `querySinkArrival`, `queryNodeSlew`, `queryNetLoad`, `queryArea`, and `queryClockIds`;
  - made `queryClockContext` and `mutableClockContext` private;
  - removed FastSTA tests that depended on public context injection and kept lower-level internal tests where they intentionally cover internals.

- Flow runtime cleanup:
  - moved `CTSRuntime` into `Flow.hh`;
  - deleted `CTSRuntime.hh`;
  - changed all production and test includes to use `Flow.hh`;
  - moved `readClockData`, `runSynthesis`, `runOptimization`, and `evaluateClockTree` out of the public Flow facade.

- HTree contract cleanup:
  - deleted `HTreeContracts.hh`;
  - moved public HTree contract types to nested `HTree::*` names;
  - removed old global HTree contract names;
  - kept ordinary production use on `HTree::build`;
  - moved diagnostic-only build state and `buildWithDiagnostics` to `synthesis/htree/diagnostic/HTreeDiagnostic.hh`.

- Topology and topology module cleanup:
  - removed `Topology::buildSourceTrunk` from the root Topology facade;
  - moved source-trunk input/output/summary/build contracts to `synthesis/topology/trunk/SourceTrunk.hh`;
  - narrowed `TopologyGen.hh` to `TopologyGen::build(loads, Input, Config)`;
  - moved fast-clustering calls to `FastClustering` / `Clustering` facades instead of forwarding through `TopologyGen`.

- Utility cleanup:
  - deleted obsolete production utility `RootedTreeLCA.hh` and its test/CMake target because it had no production users.

## Final Scans

- Removed header/type scan is clean for:
  - `CTSRuntime.hh`;
  - `HTreeContracts.hh`;
  - old global HTree contract names;
  - `RootedTreeLCA`;
  - removed graph utility CMake targets.
- Singleton scan shows only the allowed `CTS_API_INST` / `CTSAPI::getInst()`.
- `schema::` namespace scan is clean.
- Empty wrapper scan is clean for empty `{Input,Config,Output,Summary}` structs and `Output` containing only `Summary`.
- Public seam scan is clean for removed FastSTA facade methods, `Topology::buildSourceTrunk`, Flow partial-stage public calls, and old
  `HTree::buildWithDiagnostics`.

## Validation

- Targeted build passed:
  - `ninja -C build icts_source_flow icts_test_flow icts_source_flow_synthesis_htree icts_test_flow_synthesis_htree icts_source_flow_synthesis_topology icts_test_flow_synthesis icts_source_database_adapter_fast_sta icts_test_database_adapter_fast_sta icts_source_module_topology icts_test_module_topology_gen icts_test_module_topology_fast_clustering`
- Focused tests passed:
  - `ctest --test-dir build -R '^(icts_test_flow|icts_test_flow_synthesis|icts_test_flow_synthesis_htree|icts_test_module_topology_gen|icts_test_module_topology_fast_clustering)$' --output-on-failure`
- Full iCTS tests passed:
  - `ctest --test-dir build -R '^icts_test_' --output-on-failure`
- Pre-commit checker passed:
  - `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS --quiet`
- Whitespace check passed:
  - `git diff --check`

## Intentional Remaining Boundaries

- `HTreeDiagnostic.hh` is an internal diagnostic/test/experiment contract, not the root HTree facade. Production HTree callers should use
  `HTree.hh` and `HTree::build`.
- Optimization, characterization, report, and routing still have lower-level implementation headers where multiple translation units share real
  internal contracts. They were reviewed but not collapsed cosmetically when doing so would create churn without reducing supported public behavior.
