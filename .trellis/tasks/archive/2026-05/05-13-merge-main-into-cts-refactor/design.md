# Technical Design

## Merge Boundary

The branch under work is `cts_refactor`; the source ref is local `main` at `57d0e8b31663`, matching the previously discussed merge baseline. `origin/main` has newer remote work, but it introduces broad conflicts outside the current CTS merge task and is recorded only as a later follow-up risk. The merge must be performed as a local, uncommitted merge so the final diff can be reviewed before the user decides whether to commit.

The merge resolution policy is asymmetric:

- CTS source code, CTS-facing adapters, CTS configuration schema, and CTS external interfaces preserve `cts_refactor` behavior.
- iSTA, iPA, liberty parser, and shared database behavior are resolved by semantic correctness rather than branch preference.
- Non-CTS modules can follow `main` after confirming they do not alter CTS data contracts, binary routing, or design-script behavior.
- App-level CMake/output setup keeps the current `ics55_dev` workflow.

## Critical Semantic Contract

Liberty leakage power values must be exposed to CTS in the expected milliwatt-derived scale. The `cts_refactor` parser stores `leakage_power_unit` scale and converts both cell-level leakage and leakage group values. This is required because the ICS55 liberty declares `leakage_power_unit : "1nW"`, while CTS root-driver selection consumes leakage values through STA/liberty data.

Preserving only the `main` raw string storage is insufficient; it causes raw numeric leakage values to be interpreted as the wrong unit and changes root-driver characterization. Any merged liberty metadata/export additions from `main` must coexist with the `cts_refactor` conversion path.

## Validation Design

The runtime equivalence check uses the same iCTS command before and after merge:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Logs and selected generated outputs are saved below this task's `artifacts/` directory. The comparison focuses on:

- command success/failure,
- selected root driver and root load,
- root-driver delay/power compensation,
- final clock buffer/inverter/count metrics,
- DEF/Verilog/CTS generated output differences that are not timestamp-only noise.

## Rollback Shape

Because the merge is uncommitted, rollback is `git merge --abort` while still in merge state. If conflict resolution completes and the repository leaves merge state, rollback requires resetting only the merge worktree changes; that must not be done without user approval because Trellis archive/task files are also intentionally dirty.
