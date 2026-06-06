# Engineering-Optimal Liberty Parser Runtime Plan

## Goal

Optimize ECC Liberty parsing/loading at the engineering-architecture level while
preserving Liberty input/output contracts and algorithmic correctness across the
whole ECC project.

The target is not another local constant-factor patch. The target is to remove
avoidable full-pipeline work in the current implementation:

```text
Liberty text
  -> scanner tokens
  -> raw AST object graph
  -> C wrapper statement/value objects
  -> LibertyReader visitor
  -> iSTA semantic Liberty model
```

and move toward:

```text
Liberty text
  -> scanner/parser events
  -> iSTA semantic Liberty model
```

while keeping the existing raw parser API available for all ECC call sites that
depend on it.

## Current Baseline

The previous committed baseline is:

- `0ec2a934b perf(ista): optimize liberty link dispatch`
- `ca495af0f chore(task): archive 06-06-optimize-runtime-non-cts-read-data`
- `08fa658dc chore: record journal`

Current profiling artifacts are saved under:

```text
.trellis/tasks/06-06-profile-read-data-hotspots/artifacts/current_perf/
```

The current measured `read_data` split from that run is:

| Bucket | Runtime |
| --- | ---: |
| `read_data` | 10.488 s |
| Liberty raw load/parse | 5.623 s |
| Liberty link/object construction | 4.723 s |
| Post-Liberty read-data work | 0.141 s |
| CTS total | 21.626 s |

Perf attribution showed:

- raw parse path: `LibertyScanner::yylex`, `readString`, `getChar`,
  `std::istream::get`, `LibertyDriver::createComplexAttr`,
  `LibGroup::addAttribute`.
- link path: `LibertyReader::visitAxisOrValues`, `visitPowerTable`,
  `visitTable`, `visitInternalPower`, `visitTiming`.

The key conclusion is that the current parser is already linear in Big-O terms,
but it is not work-optimal: it performs multiple full linear passes and builds
intermediate representations that are not the final ECC/iSTA Liberty output.

## Non-Negotiable Contracts

### Liberty Input Contract

The optimized implementation must accept the same Liberty input language that
the current ECC parser accepts, including:

- same `.lib` file entry points.
- same group nesting and attribute grammar.
- same handling of comments, escaped newlines, strings, identifiers, numeric
  values, `k/K` suffixes, quoted comma-delimited table values, and bus/index
  identifiers.
- same tolerated current behavior for unsupported groups/attributes unless a
  separate correctness bug is explicitly approved.
- same error/failure behavior for malformed input unless explicitly documented
  as a compatibility-safe fix.

### Liberty Output Contract

The optimized implementation must preserve observable output for all ECC users:

- `Lib::loadLibertyWithCppParser(const char*)` must produce an equivalent iSTA
  `Lib` semantic model.
- Existing C parser API signatures and ownership semantics must remain
  compatible:
  - `liberty_parse_lib`
  - `liberty_free_lib_group`
  - `liberty_convert_raw_group_stmt`
  - `liberty_convert_group_stmt`
  - `liberty_convert_simple_attribute_stmt`
  - `liberty_convert_complex_attribute_stmt`
  - `liberty_convert_string_value`
  - `liberty_convert_float_value`
  - corresponding `liberty_free_*` functions.
- Callers that consume raw `LibertyGroupStmt`, `LibertySimpleAttrStmt`,
  `LibertyComplexAttrStmt`, `LibertyVec`, `LibertyStringValue`, and
  `LibertyFloatValue` must continue to observe equivalent data.
- iCTS QoR and iSTA Liberty semantics must not change.

### Correctness Contract

The optimized path must preserve algorithmic correctness:

- Same Liberty cell/pin/bus/timing/power/template semantic construction.
- Same unit conversions and scales.
- Same table axis/value ordering.
- Same timing and internal-power relation handling.
- Same cell/port/arc ownership relationships.
- Same behavior for duplicate raw attributes where current behavior is
  observable.
- Same `LibBuilder` semantic construction order, unless equivalence is proven
  by validation.

## Engineering-Optimal Architecture

### Core Design

Introduce a parser event layer that decouples syntax recognition from the output
representation.

Conceptually:

```cpp
class LibertyParseSink {
 public:
  void beginGroup(name, params, location);
  void endGroup(location);
  void simpleAttr(name, value, location);
  void complexAttr(name, values, location);
  void variable(name, value, location);
};
```

The parser emits events once. Different sinks can consume the same event stream:

| Sink | Purpose |
| --- | --- |
| `RawAstSink` | Builds the existing raw AST for compatibility APIs. |
| `StaSemanticSink` | Builds the iSTA `Lib` semantic model directly. |
| `Trace/CompareSink` | Optional validation sink for equivalence testing. |

The compatibility path remains available:

```text
liberty_parse_lib -> parser events -> RawAstSink -> existing C wrapper API
```

The optimized iSTA path becomes:

```text
Lib::loadLibertyWithCppParser
  -> parser events
  -> StaSemanticSink
  -> final Lib model
```

This removes the raw AST and C wrapper allocation/traversal from the hot iSTA
semantic load path without breaking raw parser consumers.

### Complexity Target

The full Liberty load cannot be sublinear. The theoretical lower bound is:

```text
Omega(N + T + M + O)
```

Where:

- `N`: input characters.
- `T`: tokens/statements.
- `M`: table numeric values.
- `O`: final semantic objects.

The target implementation should keep the same asymptotic bound:

```text
O(N + T + M + O)
```

but reduce duplicate full-pass work:

| Current work | Target work |
| --- | --- |
| text scan | text scan |
| tokenization | tokenization |
| raw AST build | only on compatibility path |
| raw AST traversal | removed from optimized iSTA path |
| C wrapper allocation | removed from optimized iSTA path |
| semantic object build | semantic object build |
| table value string re-split | direct numeric parse |

## Implementation Plan

### Phase 0: Contract Capture and Golden Baseline

No source behavior change.

Deliverables:

- Define a Liberty equivalence checklist for raw parser output and iSTA
  semantic output.
- Capture current `ics55_dev` baseline:
  - CTS Runtime Overview.
  - Liberty load/link timestamp split.
  - `iCTS_metrics.json`.
- Add or identify a repeatable semantic comparison method:
  - iSTA `Lib` cell count, pin count, arc count, timing table count.
  - selected representative cell/pin/timing/power table value checks.
  - current iCTS metrics exact match.

Acceptance:

- Baseline data is recorded.
- No code path is changed.

### Phase 1: Event Parser Skeleton With Raw AST Sink

Refactor parser construction so the current recursive parser can emit events to
a sink while `RawAstSink` reproduces the existing raw AST.

The external raw parser API must still behave as before. This phase is a
mechanical architecture separation, not a performance phase.

Deliverables:

- `LibertyParseSink` internal contract.
- `RawAstSink` implementation that builds `LibGroup`, `LibAttribute`,
  `LibValue`, and variables exactly as the current `LibertyDriver` does.
- `liberty_parse_lib` still returns a handle compatible with existing
  `liberty_convert_*` functions.

Acceptance:

- Existing iCTS binary run produces identical metrics.
- Raw wrapper traversal remains compatible.
- Runtime must not regress materially; small refactor noise is acceptable at
  this stage.

### Phase 2: Direct iSTA Semantic Sink

Implement `StaSemanticSink` that directly builds the iSTA `Lib` semantic model
from parser events.

This sink should reuse the existing `LibertyReader` semantic logic where
possible, but it must not require constructing a full raw AST or allocating C
wrapper statement objects.

Deliverables:

- Direct semantic builder for:
  - library groups.
  - cell groups.
  - pin/bus/bundle groups.
  - timing groups.
  - internal_power groups.
  - LUT templates and table groups.
  - scalar simple/complex attributes currently consumed by `LibertyReader`.
- Compatibility mode to compare old raw-AST visitor output against the new
  direct semantic sink during validation.

Acceptance:

- `Lib::loadLibertyWithCppParser` returns equivalent semantic data.
- iCTS metrics match exactly.
- `read_data` improvement is measured.

### Phase 3: Direct Numeric Table Parsing and Storage Discipline

Optimize numeric-heavy Liberty payload handling inside the event/semantic path.

Deliverables:

- Parse quoted comma-delimited table values directly into numeric sequences.
- Avoid intermediate `vector<string>` and repeated string copies.
- Use contiguous numeric storage where this does not change public output.
- Provide adapters only where existing public APIs require `LibAttrValue`
  objects.

Acceptance:

- Table axis/value ordering and numeric values are equivalent.
- Timing/power table semantic checks match.
- iCTS metrics match exactly.

### Phase 4: Scanner Throughput and Memory Ownership

Optimize input scanning and allocation while preserving token behavior.

Deliverables:

- Buffered or memory-mapped scanner for seekable files.
- Existing behavior-compatible fallback for non-seekable streams.
- Line/column tracking preserved.
- Avoid per-character iostream calls in the normal file path.
- Introduce arena/pool allocation only if ownership and destruction remain
  equivalent for all public handles.

Acceptance:

- Raw and semantic outputs remain equivalent.
- Peak memory is reported.
- Runtime improvement is measured.

### Phase 5: Compatibility Facade and Rollout

Make the optimized direct semantic path the default for
`Lib::loadLibertyWithCppParser`, while keeping the raw AST compatibility path for
callers that use `liberty_parse_lib` and `liberty_convert_*`.

Deliverables:

- Clear facade boundary:
  - iSTA semantic load uses direct sink.
  - raw C API uses raw AST sink.
- Debug/validation switch to compare direct semantic load against the legacy
  visitor path during rollout.
- Documentation of preserved contracts and known unsupported Liberty features.

Acceptance:

- ECC Liberty public input/output contracts remain unchanged.
- iCTS binary validation passes.
- No CTS source changes.
- No iSTA `ecc_dev_tools` check is run for this task, per user constraint.

## Validation Plan

Validation must be binary- and contract-driven.

Required command after each implementation phase:

```bash
ninja -C /home/liweiguo/project/ecc-tools-dev/build iEDA
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

For each phase record:

- `read_data` runtime.
- CTS total runtime.
- Liberty raw load / semantic build timestamp split when available.
- `iCTS_metrics.json`.
- Any equivalence comparison output.
- Peak RSS or VMEM when the scanner/buffer changes.

Do not run:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iSTA
```

## Success Criteria

- No ECC Liberty input/output contract changes.
- No algorithmic correctness changes.
- No CTS source code changes.
- Existing iCTS QoR metrics match exactly on `ics55_dev`.
- The direct semantic load path avoids full raw AST + C wrapper traversal for
  iSTA Liberty loading.
- `read_data` runtime is improved materially versus the `10.488 s` current
  profiling baseline.
- Residual raw AST path remains available for compatibility users.

## Out of Scope

- CTS synthesis, optimization, instantiation, or evaluation optimization.
- CTS source changes.
- CTS-only Liberty subset parsing that skips data needed by general iSTA users.
- Changing public C parser API signatures.
- Changing observable Liberty semantic output to gain speed.
- Running iSTA `ecc_dev_tools` checks.
- Removing the raw AST compatibility path before all ECC call sites are proven
  independent of it.

## Risks

- Direct semantic construction may rely on subtle `LibertyReader` ordering
  assumptions. Mitigation: keep a legacy comparison mode until equivalence is
  proven.
- Raw C wrapper APIs may have hidden consumers. Mitigation: keep `RawAstSink`
  and existing public C API intact.
- Full-file buffering can increase peak memory. Mitigation: report memory and
  keep fallback/streaming options.
- Table value direct parsing can change corner-case handling if not aligned
  with current `atof` behavior. Mitigation: explicitly preserve existing
  conversion semantics or document any approved compatibility fix.

## Current Task State

This task now contains the first implemented and binary-validated landing point:
the iSTA Liberty load path links the current raw C++ AST directly into
`LibertyReader` semantic construction, bypassing `liberty_convert_raw_group_stmt`
and C wrapper statement/value traversal in the hot iSTA path.

This is a Phase 2a implementation rather than the final parser-event sink:

```text
Liberty text
  -> current C++ raw AST
  -> LibertyReader direct C++ AST visitor
  -> iSTA semantic Liberty model
```

The existing raw C parser API remains unchanged and available for compatibility
callers.

Validated result:

| Bucket | Baseline | Direct AST | Delta | Improvement |
| --- | ---: | ---: | ---: | ---: |
| Liberty raw load/parse | 5.623 s | 5.751 s | +0.128 s | -2.3% |
| Liberty link/object construction | 4.723 s | 3.124 s | -1.599 s | 33.9% |
| `read_data` | 10.488 s | 9.014 s | -1.474 s | 14.1% |
| CTS total | 21.626 s | 20.153 s | -1.473 s | 6.8% |

Binary validation artifacts are saved under:

```text
.trellis/tasks/06-06-profile-read-data-hotspots/artifacts/direct_ast/
```

Detailed result report:

```text
.trellis/tasks/06-06-profile-read-data-hotspots/direct-ast-results.md
```

`iCTS_metrics.json` matches the current baseline exactly. No CTS source code was
changed. No iSTA `ecc_dev_tools` check was run, per user constraint.

## Final Task State

The final retained implementation is a Phase 2a + scanner optimization landing
point, not the parser-event semantic sink described as the long-term ideal
architecture.

Final path:

```text
Liberty text
  -> full-file buffered scanner
  -> current C++ raw AST
  -> LibertyReader direct C++ AST visitor
  -> iSTA semantic Liberty model
```

Compatibility path remains:

```text
liberty_parse_lib
  -> LibertyDriver::parse
  -> current C++ raw AST
  -> existing C wrapper conversion APIs
```

The event/sink semantic-frame experiment was implemented and binary-validated,
but rejected for this task because it regressed runtime:

| Variant | Metrics | `read_data` | Verdict |
| --- | --- | ---: | --- |
| semantic frame sink | bitwise match | 13.855 s | rejected |
| raw AST + buffered scanner using iterator file read | bitwise match | 12.399 s | rejected |
| raw AST + buffered scanner using `seekg/tellg/read` | bitwise match | 6.692 s | retained |

Final retained result:

| Bucket | Baseline | Direct AST | Final | Improvement vs Baseline |
| --- | ---: | ---: | ---: | ---: |
| Liberty load/parse | 5.623 s | 5.751 s | 3.500 s | 37.8% |
| Liberty link/object construction | 4.723 s | 3.124 s | 3.057 s | 35.3% |
| `read_data` | 10.488 s | 9.014 s | 6.692 s | 36.2% |
| CTS total | 21.626 s | 20.153 s | 17.622 s | 18.5% |
| `/usr/bin/time` wall | 25.08 s | 23.01 s | 20.43 s | 18.5% |
| Max RSS | 5,474,528 KB | 5,274,860 KB | 5,274,460 KB | 3.7% |

Final binary validation artifacts are saved under:

```text
.trellis/tasks/06-06-profile-read-data-hotspots/artifacts/final_buffered_raw/
```

Detailed final result report:

```text
.trellis/tasks/06-06-profile-read-data-hotspots/final-buffered-raw-results.md
```

`iCTS_metrics.json` matches both the original baseline and direct-AST artifact
bitwise. `ninja -C build iEDA` passes. `git diff --check` passes. No CTS source
code was changed. No iSTA `ecc_dev_tools` check was run, per user constraint.
