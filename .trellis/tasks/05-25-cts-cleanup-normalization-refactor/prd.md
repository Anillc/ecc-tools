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
- iCTS source currently has 353 source/header files under `source`, including 185 headers.
- iCTS tests currently have 111 source/header files under `test`.
- `FastSta.hh` exposes several public methods and structs that have no production external callers or are used only for tests.
- `CTSRuntime.hh` is a root flow peer header with one production include and many test includes; it should be folded into `Flow.hh` unless a new
  narrow justification appears during implementation.
- `HTreeContracts.hh` is a root HTree peer header that carries both public HTree contracts and implementation/diagnostic transport; it should be
  folded into `HTree.hh`, with ordinary output separated from diagnostics.
- Multiple flow/module helper headers are included outside their owning subdirectories, so the task must include a CMake/include visibility pass.

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

- [ ] A comprehensive audit records public interfaces and standalone headers that are unused, test-only, implementation-only, or truly public.
- [ ] `FastSta.hh` public surface is reduced to production-facing service APIs only.
- [ ] `FastSTA` context internals are not exposed through public test-only or mutable accessors.
- [ ] Test-only FastSTA setup/inspection is moved into test code or test helpers.
- [ ] `HTreeContracts.hh` is either folded into `HTree.hh` or explicitly justified with a narrow production dependency reason.
- [ ] `CTSRuntime.hh` is either folded into `Flow.hh` or explicitly justified with a narrow production dependency reason.
- [ ] Similar module-level contract headers are reviewed and either collapsed, privatized, or documented as intentionally public.
- [ ] CMake visibility matches the new header boundaries; public dependencies are not kept just for private helpers.
- [ ] No new singleton, global service locator, or broad runtime context is introduced.
- [ ] Targeted iCTS builds and tests pass.
- [ ] Full `icts_test_*` suite passes before implementation handoff.
- [ ] `ecc_dev_tools` is not run until explicitly requested by the user.

## Out Of Scope

- Changing `CTSAPI` external behavior.
- Algorithmic QoR changes unrelated to API/header cleanup.
- Broad formatting-only rewrites.
- Re-architecting HTree/Optimization algorithms beyond what is needed to hide private implementation detail and remove unused/test-only APIs.

## Current Notes

- The immediately preceding task was committed as `6c437f228`.
- Current working tree intentionally still has two untracked temporary files left out of commits: `a.out` and `tmpkwi7mspm.cc`.
- User requested commit and archive first, then deep investigation and a detailed task list. This task is currently planning-only until the user
  approves implementation.
