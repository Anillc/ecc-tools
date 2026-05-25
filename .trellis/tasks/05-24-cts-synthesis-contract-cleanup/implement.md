# Implementation Plan · CTS Synthesis Contract Cleanup

## Steps

- [x] Audit existing synthesis structs:
  `rg -n 'Options|Result|BuildOptions|BuildResult' src/operation/iCTS/source/flow/synthesis src/operation/iCTS/source/module`
- [x] Introduce `HTreeInput`, `HTreeConfig`, `HTreeOutput`, `HTreeSummary` with fields classified from current options/result.
- [x] Migrate `HTree::build` and call sites.
- [x] Move DBU, reporter, char library, STA adapter, clock period, naming and semantic role out of HTree config.
- [x] Split HTree design payload from summary/report data.
- [x] Apply same contract split to Topology sink branch/source trunk/sink load clustering where broad options/results exist.
- [x] Split CharacterizationLibrary/build orchestration runtime dependencies from true char config and summary.
- [x] Update synthesis tests.
- [x] Build and audit remaining broad struct names.

## Validation

```bash
rg -n 'struct .*Options|struct .*Result|BuildOptions|BuildResult' src/operation/iCTS/source/flow/synthesis src/operation/iCTS/source/module
bash build.sh
```

The grep is an audit, not an absolute ban. Remaining names must be locally justified or converted before parent acceptance.

## Risk Files

- `src/operation/iCTS/source/flow/synthesis/htree/HTreeSynthesisOptions.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/HTreeSynthesisResult.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.*`
- `src/operation/iCTS/source/flow/synthesis/htree/characterization/library/*`
- `src/operation/iCTS/source/flow/synthesis/topology/*`
- `src/operation/iCTS/source/module/characterization/*`
- `src/operation/iCTS/source/module/topology/*`
- `src/operation/iCTS/test/**htree**`

## Rollback Point

Complete HTree contract migration before broadening to Topology/Characterization. Do not leave call sites with both old and new HTree contracts long-term.
