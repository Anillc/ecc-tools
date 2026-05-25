# CTS cleanup and normalization refactor

## Goal

Clean and normalize the iCTS code surface after the runtime-boundary refactor. The code should expose only the business-facing contracts that
external callers of each module actually need, while implementation helpers, test-only seams, and internal contracts stay inside the owning module
or under tests.

The primary value is readability and maintainability: when a reader opens a module-level `*.hh`, it should show the module's real public contract,
not every helper type, diagnostic shape, or temporary implementation detail used by the module.

## Confirmed Facts

- Previous CTS runtime-boundary/desingleton work was committed before this task was created.
- Prior Trellis tasks from the previous CTS refactor batch were archived before this task was created.
- Current iCTS singleton scan shows only `CTS_API_INST` / `CTSAPI::getInst()` remains, which matches the allowed API-boundary exception.
- iCTS source currently has 351 source/header files under `source` after deleting obsolete utility/contract headers.
- iCTS tests currently have 110 source/header files under `test` after deleting the obsolete graph utility test.
- `FastSta.hh` exposes several public methods and structs that have no production external callers or are used only for tests.
- `CTSRuntime.hh` is a root flow peer header with one production include and many test includes; it should be folded into `Flow.hh` unless a new
  narrow justification appears during implementation.
- `HTreeContracts.hh` is a root HTree peer header that carries both public HTree contracts and implementation/diagnostic transport; it should be
  folded into `HTree.hh`, with ordinary output separated from diagnostics.
- Multiple flow/module helper headers are included outside their owning subdirectories, so the task must include a CMake/include visibility pass.
- Final implementation also tightened same-class surfaces found during review: Flow partial-stage methods, Topology source-trunk construction, HTree
  diagnostics, and TopologyGen fast-clustering convenience forwarding.

## Planning Artifacts

- `research/public-surface-audit.md`: evidence-backed public-surface and header-boundary audit.
- `design.md`: boundary principles and target architecture for this cleanup.
- `implement.md`: phased implementation checklist and validation commands.

## User Requirements

- Audit all CTS code for the same class of issue found in `FastSta.hh`:
  - unused public interfaces;
  - interfaces used only by tests;
  - public accessors that expose internal mutable state;
  - helper contracts that should live in implementation files or lower private headers;
  - redundant overloads whose production semantics are not both used.
- Remove unused interfaces.
- Move test-only behavior into tests or test helpers. Production headers must not expose methods only to let tests inject or inspect internals.
- At each module directory boundary, keep the primary `Name.hh` / `Name.cc` pair as the only external-facing contract whenever practical.
- Keep lower-level helper headers only when another production module genuinely needs them. Internal helper headers should be private to their
  target/subdirectory and should not be treated as module public API.
- Specifically investigate and plan cleanup for:
  - `source/database/adapter/fast_sta/FastSta.hh` / `.cc`;
  - `source/flow/synthesis/htree/HTreeContracts.hh`, which should likely be folded into `HTree.hh`;
  - `source/flow/CTSRuntime.hh`, which should likely be folded into `Flow.hh`;
  - similar standalone contract/helper headers across `source/flow`, `source/database/adapter`, and `source/module`.
- Preserve CTS external API compatibility at `CTSAPI` unless an intentional API change is explicitly justified.
- Preserve current behavior, reports, tests, and real-flow compatibility unless a change is explicitly recorded.

## Acceptance Criteria

- [x] A comprehensive audit records public interfaces and standalone headers that are unused, test-only, implementation-only, or truly public.
- [x] `FastSta.hh` public surface is reduced to production-facing service APIs only.
- [x] `FastSTA` context internals are not exposed through public test-only or mutable accessors.
- [x] Test-only FastSTA setup/inspection is moved into test code or test helpers.
- [x] `HTreeContracts.hh` is folded into the HTree boundary and removed.
- [x] `CTSRuntime.hh` is folded into `Flow.hh` and removed.
- [x] Similar module-level contract headers are reviewed and either collapsed, privatized, or documented as intentionally public.
- [x] CMake visibility matches the new header boundaries; public dependencies are not kept just for private helpers.
- [x] No new singleton, global service locator, or broad runtime context is introduced.
- [x] Targeted iCTS builds and tests pass.
- [x] Full `icts_test_*` suite passes before implementation handoff.
- [x] `ecc_dev_tools` was not run until explicitly requested by the user, then the final full iCTS check passed before commit.

## Out Of Scope

- Changing `CTSAPI` external behavior.
- Algorithmic QoR changes unrelated to API/header cleanup.
- Broad formatting-only rewrites.
- Re-architecting HTree/Optimization algorithms beyond what is needed to hide private implementation detail and remove unused/test-only APIs.

## Current Notes

- The immediately preceding task batch was already committed before this cleanup task started.
- The task implementation was kept uncommitted until the user requested and received a full pre-commit `ecc_dev_tools` pass.
- Implementation completed in this task:
  - narrowed `FastSTA` by removing unused/test-only facade APIs and keeping context lookup/mutation private;
  - folded `CTSRuntime.hh` into `Flow.hh`;
  - folded HTree public contracts under `HTree.hh`, removed `HTreeContracts.hh`, and moved diagnostic-only build details to
    `synthesis/htree/diagnostic/HTreeDiagnostic.hh`;
  - privatized Flow partial-stage methods behind the `runCTS` lifecycle entry;
  - moved source-trunk build contracts out of `Topology.hh` into the trunk implementation boundary;
  - reduced `TopologyGen.hh` to the explicit `build(loads, Input, Config)` contract and moved fast-clustering calls to the clustering facades;
  - removed the production/test `RootedTreeLCA` utility because it had no production users and only test-owned coverage.
- Final scans are clean for removed headers/types, `schema::` namespace uses, empty `{Input,Config,Output,Summary}` wrappers, and `_INST` uses except
  the allowed `CTS_API_INST`.
- Validation passed:
  - `ninja -C build icts_source_flow icts_test_flow icts_source_flow_synthesis_htree icts_test_flow_synthesis_htree icts_source_flow_synthesis_topology icts_test_flow_synthesis icts_source_database_adapter_fast_sta icts_test_database_adapter_fast_sta icts_source_module_topology icts_test_module_topology_gen icts_test_module_topology_fast_clustering`
  - `ctest --test-dir build -R '^icts_test_' --output-on-failure`
  - `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS --quiet`
  - `git diff --check`
