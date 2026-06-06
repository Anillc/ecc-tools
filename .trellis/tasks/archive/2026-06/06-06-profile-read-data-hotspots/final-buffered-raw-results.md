# Final Buffered Raw Parser Result

## Implemented Path

Final retained path:

```text
Liberty text
  -> full-file buffered scanner
  -> current C++ raw AST
  -> LibertyReader direct C++ AST visitor
  -> iSTA semantic Liberty model
```

The public raw C parser API remains compatible:

```text
liberty_parse_lib
  -> LibertyDriver::parse
  -> C++ raw AST
  -> existing liberty_convert_* wrappers
```

CTS source code was not changed.

## Implementation Summary

- `CppLibertyDriver::parse(const char*)` now reads the Liberty file in binary
  mode with `seekg/tellg/read` and feeds `LibertyScanner::setInputBuffer`.
- `CppLibertyScanner` keeps the existing stream path, but adds a buffer path
  for:
  - direct character reads and peeks.
  - no-escape quoted strings via span duplication.
  - identifiers via span duplication.
  - numeric tokens using a stack buffer plus `strtod`.
- `LibertyReader::linkLib()` consumes `liberty::LibGroup` directly instead of
  building and traversing `LibertyGroupStmt`/`LibertySimpleAttrStmt`/
  `LibertyComplexAttrStmt` C wrapper objects.
- Table axis/value conversion uses direct raw `LibValueList` access and keeps
  existing string comma-split behavior for quoted table values.

## Rejected Experiment

An event/sink semantic-frame prototype was built and binary-validated, but not
retained:

| Variant | Metrics | `read_data` | Verdict |
| --- | --- | ---: | --- |
| semantic frame sink | bitwise match | 13.855 s | rejected, runtime regression |

Root cause: the temporary semantic-frame sink still built a second intermediate
object graph with many `std::string`, `unique_ptr`, and vector frame moves. That
removed the C wrapper traversal but added more parse-side allocation/copy cost,
so it was worse than direct C++ AST replay.

## Final Binary Validation

Command:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
/usr/bin/time -v ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Artifacts:

```text
.trellis/tasks/06-06-profile-read-data-hotspots/artifacts/final_buffered_raw/
```

Metric checks:

```text
cmp current_perf/iCTS_metrics.json final_buffered_raw/iCTS_metrics.json -> 0
cmp direct_ast/iCTS_metrics.json final_buffered_raw/iCTS_metrics.json -> 0
```

Final measured runtime:

| Bucket | Baseline | Direct AST | Final | Delta vs Baseline | Improvement vs Baseline |
| --- | ---: | ---: | ---: | ---: | ---: |
| Liberty load/parse | 5.623 s | 5.751 s | 3.500 s | -2.123 s | 37.8% |
| Liberty link/object construction | 4.723 s | 3.124 s | 3.057 s | -1.666 s | 35.3% |
| Post-Liberty read-data work | 0.141 s | 0.139 s | 0.135 s | -0.006 s | 4.3% |
| `read_data` | 10.488 s | 9.014 s | 6.692 s | -3.796 s | 36.2% |
| CTS total | 21.626 s | 20.153 s | 17.622 s | -4.004 s | 18.5% |
| `/usr/bin/time` wall | 25.08 s | 23.01 s | 20.43 s | -4.65 s | 18.5% |
| Max RSS | 5,474,528 KB | 5,274,860 KB | 5,274,460 KB | -200,068 KB | 3.7% |

Final Liberty timestamp split:

| Liberty | Load/parse | Link/build |
| --- | ---: | ---: |
| H7CL | 1.750 s | 1.532 s |
| H7CR | 1.750 s | 1.525 s |
| Sum | 3.500 s | 3.057 s |

## Validation Notes

- `iCTS_metrics.json` is bitwise identical to both the current baseline and the
  direct AST artifact.
- `ninja -C build iEDA` passes.
- `git diff --check` passes.
- No iSTA `ecc_dev_tools` check was run, per user constraint.
