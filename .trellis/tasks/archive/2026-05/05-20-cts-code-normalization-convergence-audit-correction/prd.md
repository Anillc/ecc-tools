# CTS code normalization convergence audit and correction

## Goal

Run a convergence-level audit and correction pass over the iCTS normalization work after user review found that some structural and naming goals were
not fully satisfied.

This task must turn the previous refactor into a cleaner, more stable CTS source structure: behavior directories expose only their intended CTS
entry contracts, large flat modules are split by CTS responsibility, and newly introduced names are reviewed with the user before final naming cleanup.

## User-Observed Problems

- `src/operation/iCTS/source/database/adapter/fast_sta` still exposes many root-level headers besides `FastSta.hh/.cc`.
  The intended shape is that the directory root exposes only the necessary `FastSta` external contract, while implementation details live under
  cohesive subfolders and targets.
- The same root-contract problem may exist outside FastSTA and must be audited across iCTS source, not fixed only for one directory.
- `src/operation/iCTS/source/database/io` now contains names such as `Writeback` and `Membership`.
  The user prefers the previous visible file naming style there and does not want these terms in the directory's external shape.
- Large flat directories such as `src/operation/iCTS/source/module/characterization` and
  `src/operation/iCTS/source/module/routing/bound_skew_tree` still have too many files in one root directory.
  These should be split into responsibility subfolders, leaving the root `.hh/.cc` files as external contracts.
- Newly added files still include names that may not match CTS business semantics. This must be handled after the structural convergence work:
  list genuinely new files, excluding files that were only moved, and ask the user for naming direction before renaming.

## Confirmed Facts

- Previous parent task `.trellis/tasks/05-19-cts-code-normalization-refactor-research` is marked `completed`.
- Current task `.trellis/tasks/05-20-cts-code-normalization-convergence-audit-correction` is in `planning` status.
- Current branch is `cts_refactor`; no commit has been made for the previous refactor.
- User requested this turn to create the task and analyze only. iCTS source implementation is not in scope for this planning turn.
- User confirmed the proposed convergence rule: behavior directories with a clear CTS entry contract should be strict, while stable database/data-model
  directories can be documented exceptions when their exposed headers are real CTS domain objects.
- User accepted the recommended `database/io` correction direction: prefer writer-local implementation around `WrapperClockWriter` instead of
  visible root helper files using `Writeback` or `Membership`.
- Current direct source directory counts show several convergence candidates:
  - `module/characterization`: 22 direct `.hh/.cc` files, 10 direct headers, no child directories.
  - `module/routing/bound_skew_tree`: 21 direct `.hh/.cc` files, 6 direct headers, no child directories.
  - `flow/synthesis/htree/analytical_solver`: 14 direct `.hh/.cc` files, 5 direct headers, no child directories.
  - `database/adapter/sdc`: 14 direct `.hh/.cc` files, 5 direct headers, no child directories.
  - `module/topology/fast_clustering`: 12 direct `.hh/.cc` files, 2 direct headers, no child directories.
  - `database/adapter/sta`: 10 direct `.hh/.cc` files, 2 direct headers, no child directories.
  - `flow/synthesis/htree/solution`: 10 direct `.hh/.cc` files, 5 direct headers, no child directories.
  - `database/io`: 6 direct `.hh/.cc` files and user-confirmed naming concerns.
- `database/design` and `database/characterization` also contain multiple root headers, but they are data-model directories. They should be audited
  as possible documented exceptions instead of automatically forcing a single-entry shape.

## Requirements

- Define an iCTS directory external-contract rule before implementing changes.
- Apply that rule differently to behavior directories and stable domain-data directories:
  - Behavior directories with one clear CTS entry contract should expose only the root contract files and keep implementation headers in subfolders.
  - Stable data-model directories may expose multiple CTS domain objects when each object is an intentional external data contract.
- Audit all iCTS source directories for root-contract exposure problems, not only FastSTA.
- Correct the FastSTA root so the directory root exposes only `FastSta.hh/.cc` and `CMakeLists.txt`.
- Remove production dependencies on root-level FastSTA helper headers from other source directories.
- Keep FastSTA implementation split into cohesive subfolders and CMake targets, but prevent those targets from becoming the public source include
  surface.
- Correct `database/io` naming according to user preference:
  - eliminate the visible `Writeback` and `Membership` file/type naming introduced by the previous task unless the user explicitly approves a more
    specific CTS alternative;
  - prefer writer-owned names or local implementation details associated with `WrapperClockWriter`.
- Split large flat behavior directories into semantic subfolders where the root currently mixes external contracts, algorithms, storage, geometry,
  conversion, sampling, and helper types.
- Prioritize the user-called-out large directories first: `module/characterization` and `module/routing/bound_skew_tree`.
- Review secondary candidates after the primary fixes: `flow/synthesis/htree/analytical_solver`, `database/adapter/sdc`,
  `module/topology/fast_clustering`, `database/adapter/sta`, and `flow/synthesis/htree/solution`.
- Keep naming cleanup for newly added files as the final child step after the folder structure stabilizes.
- Before final naming cleanup, classify newly added files into:
  - true new source files;
  - moved files;
  - moved-and-edited files that still preserve an old responsibility;
  - generated or test-only files.
- Do not rename newly added files with uncertain CTS names until the user reviews the list and gives naming direction.
- Preserve runtime behavior unless a change is explicitly documented as behavior-affecting.
- Source cleanup has priority. Tests are cleaned and adjusted after source structure and names are stable.
- Do not commit or archive unless the user explicitly asks.

## Acceptance Criteria

- [x] `research.md` records the current audit evidence: root file counts, exposed headers, external include use, and large flat directories.
- [x] `design.md` defines the behavior-directory external-contract rule and documented exception policy for stable data-model directories.
- [x] FastSTA root contains only `FastSta.hh`, `FastSta.cc`, and `CMakeLists.txt`.
- [x] Production source outside `database/adapter/fast_sta` does not include FastSTA implementation headers directly.
- [x] FastSTA subfolder targets keep cohesive responsibilities without exposing implementation include paths as the broad public contract.
- [x] `database/io` no longer exposes files or public helper types named with `Writeback` or `Membership`; the final names match user-approved CTS
      wording or are writer-local implementation details.
- [x] `module/characterization` is split into responsibility subfolders; the root keeps only the intended characterization external contract.
- [x] `module/routing/bound_skew_tree` is split into responsibility subfolders; the root keeps only the intended bound-skew routing external contract.
- [x] Secondary flat directories are either corrected or documented as accepted exceptions with a concrete reason.
- [x] A final naming-review list is produced for genuinely new files, excluding pure moves, before any uncertain renames are applied.
- [x] The final source scan has no banned copied-state or vague structural names in iCTS source, including `snapshot`, `Internal`, `Support`,
      `Request`, `Response`, `Types`, `rollback`, `fallback`, `Input`, `Session`, and the newly rejected `Writeback` / `Membership`, except where
      the term is part of an explicitly approved CTS domain concept or a non-structural user-facing report string.
- [x] Focused build targets pass after structural changes.
- [x] Final iCTS validation passes:
      `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.
- [x] The user-reviewed `ics55_dev` command is run after the relevant implementation changes if source changes could affect runtime behavior.

## Out Of Scope

- Changing CTS algorithm behavior as part of directory cleanup.
- Tuning optimization runtime again unless a structural change reintroduces the fanout-4 issue.
- Committing, archiving, or reverting the previous uncommitted refactor without explicit user instruction.
- Finalizing names for uncertain newly added files before the user reviews the post-convergence file list.

## Confirmed Decisions

- Enforce the root-contract rule strictly for behavior directories with a clear CTS entry, including FastSTA, characterization, bound-skew routing,
  fast clustering, and H-tree solver-style directories.
- Allow stable database/data-model directories such as `database/design` to remain multi-header roots only when each exposed header is a real CTS
  domain object and the exception is documented.
- For `database/io`, fold writer-only helpers back into `WrapperClockWriter.cc` when feasible. If a split remains necessary, keep it writer-owned and
  avoid visible root helper names using `Writeback` or `Membership`.
- Keep uncertain new-file naming cleanup until after folder convergence, then present true new files to the user before renaming.

## Open Questions

- No planning-blocking questions remain before activation. Any new naming uncertainty found during implementation must be listed for user review
  before the affected rename is applied.
