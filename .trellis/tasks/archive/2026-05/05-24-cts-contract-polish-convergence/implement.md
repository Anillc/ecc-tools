# CTS contract polish implementation plan

## Audit

- List all production `Input`/`Config`/`Output`/`Summary` structs in `src/operation/iCTS/source`.
- Find empty contracts and summary-wrapper outputs.
- List `Options`/`Result` names in `source/flow` and `source/module`.
- Record each finding in `research/contract-audit.md` as fixed, accepted local vocabulary, or follow-up exception.

## Edits

- Remove empty config/input/output/summary structs and update signatures/call sites.
- Replace summary-only output wrappers with direct summary returns.
- Rename or split public `Options`/`Result` contracts where the iCTS taxonomy is clearer.
- Convert remaining public flow/module stage facades with long runtime dependency parameter lists into named input contracts.
- Add CTS-level `SchemaWriter` spelling for business signatures and remove `schema::SchemaWriter` from flow/module/test-facing declarations.
- Slim HTree/topology/source-trunk summaries so detailed HTree diagnostics stay local to HTree report/build code or test-only observation paths.
- Keep local low-level algorithm helper names only when they improve readability and are documented in the audit.
- Re-run targeted greps after each wave of edits and continue until the remaining hits are intentional.

## Validation

- `git diff --check`
- Targeted iCTS build targets affected by flow/module contracts.
- `ctest --test-dir build -R '^icts_test_' --output-on-failure`
- Singleton boundary grep for `_INST` and `getInst`.
- Contract convergence greps for empty structs and summary wrappers.
- Grep that `schema::SchemaWriter` no longer appears in business signatures.
- Grep that `Topology::Summary` / `SourceTrunkSummary` do not embed full `HTree::Summary`.
- Representative `ics55_dev` iCTS Tcl flow, including DEF/Verilog/report/metric/visualization outputs.
- Final only after convergence: `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`

## Completion notes

- Update `prd.md` acceptance checkboxes when verified.
- Keep the final report focused on contract changes, remaining justified local vocabulary, and validation results.
