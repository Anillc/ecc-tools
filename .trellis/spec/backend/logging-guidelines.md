# Logging Guidelines

CTS logs are plain text and stage-oriented.

## Rules
- Wrap major phases with `ieda::Stats` and `LOG_INFO`.
- Use `CTSAPIInst.logTitle/saveToLog/logEnd` for report files that mirror console stages.
- For user-facing report sections, mirror the body to both terminal logging and `cts.log`; do not leave terminal output with only a section title.
- Include context keys in logs: level, net name, pin count, file path, or runtime.
- Keep tight-loop logs behind debug macros or dedicated debug code paths.
- Initialize logging explicitly in test or binding-only entrypoints when no main flow exists.
- Keep file-only debug traces on `saveToLog`; use an explicit mirror helper for summary text that must appear in both sinks.

## Examples
- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/source/module/router/Router.cc`
- `src/operation/iCTS/source/data_manager/config/JsonParser.cc`
- `src/operation/iCTS/test/TreeBuilderTest.cc`

## Avoid
- Logging generic "failed" without the object that failed.
- Spamming per-pin logs in production paths.
- Writing a second ad-hoc logging format inside CTS.
- Printing report tables to only one sink when the section is part of the normal CTS summary.
