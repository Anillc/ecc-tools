# CTS Cleanup Completion Audit

## Public Surface Results

- `FastSta.hh` now exposes CTS-facing timing/power service APIs only. Context injection, context lookup/mutation, unused scalar queries, redundant
  route injection, and single-buffer edit facade APIs were removed from the public class.
- `Flow.hh` owns `CTSRuntime` directly and no longer exposes partial-stage methods as public APIs. The public flow surface is lifecycle/reporting
  oriented.
- `HTree.hh` is the production HTree facade. `HTreeContracts.hh` was deleted, public HTree contracts are nested under `HTree`, and diagnostics moved
  to the internal `synthesis/htree/diagnostic/HTreeDiagnostic.hh` boundary.
- `Topology.hh` no longer exposes source-trunk construction. Source-trunk contracts live under `synthesis/topology/trunk`.
- `TopologyGen.hh` now exposes only the explicit `build(loads, Input, Config)` entry. Fast clustering is reached through `FastClustering` and
  `Clustering` instead of the TopologyGen facade.
- `RootedTreeLCA.hh` and its test target were deleted because they had no production users.

## Scans

- Removed contract/header scan: clean for `CTSRuntime.hh`, `HTreeContracts.hh`, old HTree global contract names, `RootedTreeLCA`, and deleted graph
  utility targets.
- Singleton scan: only `CTS_API_INST` / `CTSAPI::getInst()` remains.
- Namespace scan: no `schema::` namespace usage remains in iCTS source/api/tests.
- Wrapper scan: no empty `{Input,Config,Output,Summary}` structs and no `Output` wrapper containing only `Summary`.
- Public seam scan: no remaining references to removed FastSTA facade APIs, `Topology::buildSourceTrunk`, public Flow partial-stage calls, or
  `HTree::buildWithDiagnostics`.

## Validation

- Targeted iCTS build passed.
- Focused Flow/Topology/HTree/TopologyGen/FastClustering tests passed.
- Full `icts_test_*` suite passed: 14/14 tests.
- Full `ecc_dev_tools` iCTS pre-commit check passed:
  - `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS --quiet`
- `git diff --check` passed.
