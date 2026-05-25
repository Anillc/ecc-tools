# Research: contract boundary audit

- Query: Audit current `src/operation/iCTS` flow/module contracts for incomplete contract-polish PRD requirements.
- Scope: internal
- Date: 2026-05-25

## Findings

The task helper reported no active task:

```bash
python3 ./.trellis/scripts/task.py current --source
```

Result: failed to resolve an active task (`Current task: (none)`). I used the explicit task path from the prompt:
`.trellis/tasks/05-24-cts-contract-polish-convergence`.

Files found / reviewed:

- `.trellis/tasks/05-24-cts-contract-polish-convergence/prd.md` - contract-polish requirements and acceptance criteria.
- `.trellis/tasks/05-24-cts-contract-polish-convergence/design.md` - contract taxonomy and Options/Result convergence rule.
- `.trellis/tasks/05-24-cts-contract-polish-convergence/implement.md` - expected audit/edit/validation sequence.
- `.trellis/spec/project-constraints.md` - hard iCTS process and validation constraints.
- `.trellis/spec/backend/quality-guidelines.md` - naming, public contract, and dependency guidance.
- `.trellis/spec/backend/logging-guidelines.md` - `SchemaWriter` / report DSL boundary guidance.
- `src/operation/iCTS/source/flow/**` - production flow contracts and facades.
- `src/operation/iCTS/source/module/**` - production module contracts and facades.

### Empty Input/Config/Output/Summary Contracts

Check:

```bash
rg -n -U "struct\s+[A-Za-z0-9_]+(Input|Config|Output|Summary)\s*\{\s*\};" src/operation/iCTS/source/flow src/operation/iCTS/source/module
```

Result: passed. No direct empty production flow/module `Input`, `Config`, `Output`, or `Summary` structs found.

Parser-style follow-up check:

```bash
python3 - <<'PY'
from pathlib import Path
import re
roots=[Path('src/operation/iCTS/source/flow'),Path('src/operation/iCTS/source/module')]
files=[p for r in roots for p in r.rglob('*') if p.suffix in ('.hh','.cc')]
for p in files:
    t=p.read_text(errors='ignore')
    for m in re.finditer(r'\bstruct\s+([A-Za-z_][A-Za-z0-9_]*(?:Input|Config|Output|Summary))\s*\{', t):
        start=m.end(); depth=1; i=start
        while i < len(t) and depth:
            depth += (t[i] == '{') - (t[i] == '}')
            i += 1
        body=t[start:i-1]
        body_nc=re.sub(r'//.*|/\*.*?\*/','',body,flags=re.S)
        if not ''.join(s.strip() for s in body_nc.split()):
            print(f'{p}:{t.count(chr(10),0,m.start())+1}:{m.group(1)}')
PY
```

Result: passed. No multiline/comment-only empty contracts found.

Recommended action: none for empty contracts.

### Summary-Only Output Wrappers

Check:

```bash
rg -n -U "struct\s+[A-Za-z0-9_]+Output\s*\{\s*(?:[A-Za-z0-9_:<>]+Summary\s+[A-Za-z0-9_]+\s*;\s*)\};" src/operation/iCTS/source/flow src/operation/iCTS/source/module
```

Result: passed. No simple `{Name}Output { {Name}Summary summary; }` wrappers found.

Parser-style follow-up found one-field outputs, but they are payload outputs rather than summary wrappers:

- `src/operation/iCTS/source/flow/evaluation/Evaluation.hh:38` - `EvaluationOutput` carries `EvaluationState state`.
- `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver/AnalyticalSolver.hh:75` - `AnalyticalSolverOutput` carries candidate payload.
- `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.hh:131` - `SinkLoadRegionEntryFilterOutput` carries filtered `HTreeTopologyChar` entries.
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.hh:83` - `CandidateCharRefFilterOutput` carries filtered candidate refs.
- `src/operation/iCTS/source/module/analytical_characterization/AnalyticalCharacterization.hh:74` - `AnalyticalCharacterizationOutput` carries `AnalyticalModelCatalog`.
- `src/operation/iCTS/source/module/analytical_characterization/AnalyticalFit.hh:55` - `AnalyticalFitOutput` carries an optional fitted model.

Recommended action: none for summary-only wrappers. If the project wants to remove all one-field payload outputs, that would be a separate stricter rule than the current PRD wording.

### Public Facades With Long Runtime/Domain Parameter Lists

Check:

```bash
rg -n -U "static\s+auto\s+[A-Za-z0-9_]+\s*\([^;{}]*(Config|Design|Wrapper|STAAdapter|FastSTA|SchemaWriter|ClockLayout|CharacterizationLibrary)[^;{}]*(Config|Design|Wrapper|STAAdapter|FastSTA|SchemaWriter|ClockLayout|CharacterizationLibrary)[^;{}]*\)" src/operation/iCTS/source/flow src/operation/iCTS/source/module -g '*.hh'
```

Result: failed. Several public or header-visible flow/module helpers still expose multiple runtime/domain dependencies directly.

Primary incomplete findings:

- `src/operation/iCTS/source/flow/synthesis/distribution/ClockDistribution.hh:82` - `ClockDistribution::prepare(...)` exposes `Design&`, `Clock&`, `STAAdapter&`, clock index, sink-domain data, root-buffer config, `DomainStatusTable&`, output `ClockDistributionContext&`, and an optional root-buffer spec. This is a public synthesis-stage facade and matches the PRD's "long runtime/domain dependency parameter list" case. It is called with the long list from `src/operation/iCTS/source/flow/synthesis/Synthesis.cc:143`.
  Recommended action: introduce a named `ClockDistributionInput` contract for runtime/domain dependencies. Consider returning a small output/build object instead of mutating `ClockDistributionContext&` if that fits the existing call sites. Do not add a fake `ClockDistributionConfig`; `ClockDistributionRootBufferSpec` is already the real optional behavior/policy object.

- `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.hh:48` and `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.hh:51` - both public `addRootBufferForSinkDomain(...)` overloads expose long materialization dependency and output-reference lists. The first overload additionally resolves buffer ports through `STAAdapter`, and both populate `Inst*&`, `Pin*&`, `Pin*&`.
  Recommended action: introduce a named root-buffer insertion input/output contract, for example `SinkDomainRootBufferInput` plus `SinkDomainRootBufferOutput`, or make the long overload private/local behind the already higher-level `ClockDistribution::prepare` boundary if external tests can be adjusted.

- `src/operation/iCTS/source/flow/synthesis/htree/characterization/library/CharacterizationLibrary.hh:63` - `buildRuntimeInput(...)` is public and takes `Config&`, `Wrapper&`, `STAAdapter&`, `FastSTA&`, and `SchemaWriter&` to create `CharBuilder::Input`. Calls at `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc:78`, `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc:92`, and `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc:123` repeat the broad runtime dependency list through helper builders.
  Recommended action: either add a small named runtime characterization input contract, or move this helper behind a narrower owner boundary so public flow headers do not expose a broad runtime dependency list.

Secondary candidates to decide explicitly:

- `src/operation/iCTS/source/flow/evaluation/qor/ClockQorMetricCollector.hh:67` and `src/operation/iCTS/source/flow/evaluation/qor/ClockQorMetricCollector.hh:73` expose multiple runtime/report dependencies in public namespace helpers. These are lower-level QOR helper operations rather than top-level stage facades, but they are still header-visible flow functions.
- `src/operation/iCTS/source/flow/synthesis/trace/distance/TopologyDistanceReport.hh:39` exposes `Config&`, `Wrapper&`, `SchemaWriter&`, and a topology build. This is report helper vocabulary and may be acceptable if documented as a report-local helper.
- `src/operation/iCTS/source/flow/synthesis/htree/embedding/Embedding.hh:41` and `src/operation/iCTS/source/flow/synthesis/htree/embedding/Embedding.hh:44` expose `Design&`, `STAAdapter&`, and `HTree::Build&` in HTree helper APIs. These are local HTree mutation helpers, but if the convergence rule applies to all public headers, they should receive named inputs.
- `src/operation/iCTS/source/flow/report/visualization/drawing/Drawing.hh:102` exposes `Design&`, `Wrapper&`, and `ClockLayout&` for a public drawing builder. This likely reads as a narrow visualization helper, not a flow facade.

### Remaining Options/Result Names

Check:

```bash
rg -n "\b[A-Za-z0-9_]*(Options|Result)[A-Za-z0-9_]*\b" src/operation/iCTS/source/flow src/operation/iCTS/source/module -g '*.hh' -g '*.cc'
```

Result: failed by grep because names remain. Most are local algorithm/report vocabulary, but one public flow/report name still needs an explicit project decision.

Names that appear justified as local or external vocabulary:

- `schema::StageReportOptions` appears in stage/report DSL use sites such as `src/operation/iCTS/source/flow/Flow.cc:86`. This is logger/report DSL configuration, not an iCTS flow/module contract.
- `KMeans::Result` at `src/operation/iCTS/source/module/topology/kmeans/KMeans.hh:40` is nested generic algorithm vocabulary and not a CTS flow/module boundary.
- `BalancePointResult` at `src/operation/iCTS/source/module/routing/bound_skew_tree/algorithm/BoundSkewTreeImpl.hh:99` is bound-skew-tree balance-point algorithm vocabulary.
- `LineDistanceResult` at `src/operation/iCTS/source/module/routing/bound_skew_tree/geometry/GeomCalc.hh:66` is geometry helper vocabulary.
- Local function names and report strings such as `BuildResultFanoutHistogram`, `emitKeyResults`, `"Run Results"`, and `"CharBuilder Results"` are not public contract structs.

Name that still needs convergence or explicit exception:

- `src/operation/iCTS/source/flow/report/export/ResultExport.hh:33` and `src/operation/iCTS/source/flow/report/export/ResultExport.hh:40` expose public `ResultExportPaths` / `ResultExport` in the flow/report layer. This is not local algorithm vocabulary; it names CTS result artifact export paths. The previous audit treated it as artifact vocabulary, which is defensible, but the PRD acceptance criterion requires remaining public `Options`/`Result` names to be explicitly justified. Recommended action: either document the exception in the final convergence notes or rename to `ReportExportPaths` / `ReportExport` for full vocabulary convergence.

### SchemaWriter Spelling

Check:

```bash
rg -n "schema::SchemaWriter" src/operation/iCTS/source/flow src/operation/iCTS/source/module
```

Result: passed. No `schema::SchemaWriter` spelling remains in production flow/module business signatures.

Observed current pattern:

- `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh:63`, `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh:81`, and `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh:104` use CTS-level `SchemaWriter*` in named input contracts.
- `src/operation/iCTS/source/flow/report/Report.hh:45`, `src/operation/iCTS/source/flow/evaluation/qor/QorEvaluation.hh:104`, and `src/operation/iCTS/source/module/topology/TopologyGen.hh:57` use `SchemaWriter` without the `schema::` qualification.
- `schema::StageReportOptions` remains in implementation code for report DSL configuration, which matches the PRD boundary.

Recommended action: none for `SchemaWriter` spelling.

### HTree / Topology Summary Boundary

Check:

```bash
rg -n "HTree::Summary|HTreeSummary|htree_summary|SourceTrunkSummary|struct Summary" src/operation/iCTS/source/flow/synthesis/topology src/operation/iCTS/source/flow/synthesis/htree
```

Result: mostly passed for cross-boundary transport. Current `Topology::Summary` and `SourceTrunkSummary` no longer embed a full `HTree::Summary`.

Current code patterns:

- `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh:158` carries only topology-level status, cluster leaf distance summary, selected HTree depth/level count, and inserted object counts.
- `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh:195` carries only source-trunk status, selected depth/level count, inserted counts, and boundary-relaxation status.
- `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc:58` and `src/operation/iCTS/source/flow/synthesis/topology/Topology.cc:68` aggregate only the minimal synthesis counters from topology/source-trunk builds into `SynthesisTraceSummary`.

Remaining caveat:

- `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh:186` still defines a broad `HTreeSummary` with detailed characterization, root-driver compensation, boundary relaxation, and analytical validation fields through `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh:243`. Many fields are consumed by HTree-owned reporting in `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc:139` and nearby report assembly. This is acceptable only if `HTreeSummary` is considered owner-local to the HTree build/report stage. If the PRD is interpreted strictly as "production Summary objects should not carry report-only diagnostics at all," split report-only HTree diagnostics into an HTree report/observation object and keep `HTreeSummary` to caller-needed status/aggregation fields.

## Caveats / Not Found

- No source files were modified by this audit.
- The active-task helper did not know the current task; the output path came from the user prompt.
- The worktree is shared. Line references reflect the current worktree as read during this audit on 2026-05-25.
- No external references were needed; this was an internal code/spec audit.
- Not found: empty production flow/module `Input`/`Config`/`Output`/`Summary` structs.
- Not found: summary-only production flow/module `Output` wrappers.
- Not found: `schema::SchemaWriter` in production flow/module business signatures.
- Critical remaining action items are the long public dependency parameter lists, especially `ClockDistribution::prepare`, the `DesignConversion::addRootBufferForSinkDomain` overloads, and `CharacterizationLibrary::buildRuntimeInput`; plus the `ResultExport` naming decision if the team wants zero public flow/report `Result` names.
