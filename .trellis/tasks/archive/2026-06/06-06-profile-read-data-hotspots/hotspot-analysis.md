# read_data Hotspot Analysis

## Scope

This report profiles the current post-dispatch iSTA Liberty load/link path used
by iCTS `read_data`. CTS synthesis, optimization, instantiation, and evaluation
are not considered optimization targets here.

The current source baseline includes:

- `0ec2a934b perf(ista): optimize liberty link dispatch`
- `ca495af0f chore(task): archive 06-06-optimize-runtime-non-cts-read-data`
- `08fa658dc chore: record journal`

No source code was changed for this profiling task.

## Reproduction

Command:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
/usr/bin/time -v perf record -F 99 -g --call-graph fp \
  -o /home/liweiguo/project/ecc-tools-dev/.trellis/tasks/06-06-profile-read-data-hotspots/artifacts/current_perf/perf.data \
  -- ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Tracked artifacts:

- `artifacts/current_perf/perf.data`
- `artifacts/current_perf/perf_report_children.txt`
- `artifacts/current_perf/perf_report_no_children.txt`
- `artifacts/current_perf/perf_report_by_symbol.txt`
- `artifacts/current_perf/run.stderr`
- `artifacts/current_perf/run.stdout`
- `artifacts/current_perf/iCTS_metrics.json`

Ignored local copies, kept for this workspace but not forced through the
project-wide `*.log` ignore rule:

- `artifacts/current_perf/cts.log`
- `artifacts/current_perf/cts_detail.log`

Perf completed successfully with `2372` samples and `0` lost samples. Kernel
symbol resolution was restricted by `/proc/kallsyms`; user-space `iEDA` symbols
were still resolved and are sufficient for the read-data attribution below.

## Runtime Split

CTS Runtime Overview from the current perf run:

| Stage | Elapsed time |
| --- | ---: |
| read_data | 10.488 s |
| synthesis | 10.342 s |
| optimization | 0.623 s |
| instantiation | 0.100 s |
| evaluation | 0.056 s |
| total | 21.626 s |

`/usr/bin/time` reported `25.08 s` wall time for the full perf-wrapped process.

Liberty timestamp split inside `read_data`:

| Step | Start | End | Elapsed |
| --- | --- | --- | ---: |
| H7CL raw load/parse | 14:22:27.634305 | 14:22:30.452018 | 2.818 s |
| H7CL link | 14:22:30.452032 | 14:22:32.812160 | 2.360 s |
| H7CR raw load/parse | 14:22:32.812357 | 14:22:35.617460 | 2.805 s |
| H7CR link | 14:22:35.617475 | 14:22:37.980813 | 2.363 s |
| post-Liberty read_data work | 14:22:37.980813 | 14:22:38.121680 | 0.141 s |

Derived split:

| Bucket | Elapsed | Share of read_data |
| --- | ---: | ---: |
| Liberty raw load/parse | 5.623 s | 53.6% |
| Liberty link/object construction | 4.723 s | 45.0% |
| Clock trace/materialization/reporting after Liberty | 0.141 s | 1.3% |

Conclusion: `read_data` is still almost entirely Liberty work. There is no
meaningful remaining target in clock trace/materialization for this design.

## Perf Attribution

Children mode shows `icts::Wrapper::loadLibertyIfNeeded` at `32.00%` of the
full perf run. Its children split into:

- `ista::Lib::loadLibertyWithCppParser` / `LibertyReader::readLib` /
  `liberty_parse_lib`: `18.52%`.
- `ista::LibertyReader::linkLib`: `13.48%`.

This matches the timestamp split directionally: raw parser work is the larger
bucket, link/object construction is close behind.

Read-data-specific children-mode hotspots:

| Hotspot | Evidence | Attribution |
| --- | ---: | --- |
| `liberty::LibertyScanner::yylex` | 9.01% children, 0.12% self | raw Liberty parse |
| `liberty::LibertyScanner::readString` | 4.77% children, 0.37% self | raw Liberty parse, quoted values |
| `std::istream::get` | 2.92% self | raw Liberty parse, character-by-character scanner |
| `liberty::LibertyScanner::getChar` | 2.14% self | raw Liberty parse scanner dispatch |
| `liberty::LibGroup::addAttribute` | 3.33% children | parser AST object/index construction |
| `liberty::LibertyDriver::createComplexAttr` | 3.01% children | parser AST complex attribute construction |
| `ista::LibertyReader::visitAxisOrValues` | 8.24% children | link side table axis/value conversion |
| `visitAxisOrValues` conversion lambda | 7.17% children | link side split/atof/LibFloatValue construction |
| `ista::LibertyReader::visitInternalPower` | 7.71% children | link side power table path |
| `ista::LibertyReader::visitPowerTable` | 6.22% children | link side power table path |
| `ista::LibertyReader::visitTiming` | 4.99% children | link side timing table path |
| `ista::LibertyReader::visitTable` | 4.37% children | link side timing table path |
| `malloc_consolidate` | 3.82% self | allocator pressure across parse/link |
| `__rawmemchr_avx2` | 4.54% self | string scanning/conversion, callchain not resolved |

Important interpretation:

- `LibertyReader::visitPin` still appears high in children mode because every
  pin owns the timing and internal-power traversal below it. Non-children mode
  does not show pin-name parsing itself as a remaining self hotspot.
- CTS synthesis symbols appear in non-children mode, for example H-tree
  composition and pattern hashtable rehashing. They are out of scope for this
  task and should not drive read-data changes.

## Code-Level Observations

Link-side table conversion is concentrated in
`src/database/manager/parser/liberty/LibParserCpp.cc`:

- `visitAxisOrValues` starts at line 480.
- For string-valued table `values`, it copies the raw string, splits through
  `std::istringstream` into `std::vector<std::string>`, then converts each token
  with `std::atof`.
- Each parsed number is stored as a separate `std::unique_ptr<LibFloatValue>`.
- The path is reached from both timing tables and power tables, which explains
  the duplicated `visitTable` and `visitPowerTable` children in the report.

Raw parser cost is concentrated in
`src/database/manager/parser/liberty/CppLibertyScanner.cc` and
`src/database/manager/parser/liberty/CppLibertyDriver.cc`:

- `LibertyScanner::getChar` uses `std::istream::get` for every character and
  checks stream EOF before each read.
- `readString`, `readNumber`, and `readIdentifier` append one character at a
  time to `std::string`.
- `yylex` duplicates string tokens with `strdup`.
- `LibertyDriver::createComplexAttr` and `LibGroup::addAttribute` construct the
  raw AST and maintain `_attr_map`, `_statements`, and `_attrs`.

The raw parser has the higher wall-time ceiling, but it also touches the common
Liberty grammar/scanner path. The table-value conversion path has slightly
smaller ceiling but is much more localized.

## Ranked Optimization Candidates

1. Link-side fast path for `visitAxisOrValues`.

   This is the best first implementation target for the next code change. It is
   local to `LibParserCpp.cc`, directly tied to `read_data` link time, and
   avoids grammar-level changes. Replace `istringstream` plus intermediate
   `vector<string>` with direct comma scanning over the string value, reserve the
   destination vector, and parse with `std::strtod`/`std::from_chars` where
   available. Preserve current behavior for quoted comma-separated numbers and
   direct float values.

   Expected impact: medium. It targets the `8.24%` children-mode
   `visitAxisOrValues` bucket and should reduce a meaningful part of the current
   `4.723 s` link time. Risk: low to medium, because the change is contained and
   can be validated with the existing iCTS metric comparison and runtime split.

2. Raw scanner buffered-input optimization.

   The parser-side ceiling is the largest: raw load/parse is `5.623 s`, and perf
   points at `LibertyScanner::yylex`, `readString`, `getChar`, and
   `std::istream::get`. A performance-oriented but still bounded change is to
   keep the parser interface intact while making `LibertyScanner` read from a
   memory buffer or cached string view instead of repeatedly calling
   `std::istream::get`. This should also simplify EOF handling and reduce stream
   virtual/function-call overhead.

   Expected impact: high. Risk: medium, because every Liberty token flows
   through this scanner and parser correctness has to be preserved.

3. Parser AST allocation/index pressure.

   `createComplexAttr` and `LibGroup::addAttribute` show parser-side allocation
   and indexing cost. Potential changes include reserving vectors where group
   statement counts are known, avoiding redundant map insert work where
   attributes are never queried by name, or reducing duplicate raw AST storage
   for high-volume table values.

   Expected impact: medium. Risk: medium to high unless scoped carefully,
   because the raw AST is consumed by the existing `LibertyReader` C interface.

4. Link traversal specialization for table-heavy groups.

   `visitStmtInGroup` currently does one pass for simple attributes and a second
   pass for complex/group statements. This is structurally useful because many
   groups need simple attributes before child groups, but table-heavy timing and
   power paths pay repeated traversal and conversion overhead. Specialized
   handling for known table groups could reduce work, but this is less directly
   evidenced than `visitAxisOrValues`.

   Expected impact: small to medium. Risk: medium.

## Recommendation

Proceed with candidate 1 first: optimize `visitAxisOrValues` table-value
conversion. It is the most practical next change under the current rule set:
performance-focused, read-data-only, no CTS code, no iSTA ecc-dev check, and
minimal enough to preserve existing Liberty behavior.

After candidate 1, re-run the same perf/timestamp flow. If link time drops and
raw load/parse becomes dominant, candidate 2 should become the primary target.

## Worktree Status During Profiling

Before committing this task, the only worktree changes were this new task
directory:

```text
?? .trellis/tasks/06-06-profile-read-data-hotspots/
```
