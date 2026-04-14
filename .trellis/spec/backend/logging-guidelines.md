# Logging Guidelines

CTS logs are plain text and stage-oriented.

## Rules
- Wrap major phases with `ieda::Stats` and `LOG_INFO`.
- Use `CTSAPIInst.logTitle/saveToLog/logEnd` for report files that mirror console stages.
- Include context keys in logs: level, net name, pin count, file path, or runtime.
- Keep tight-loop logs behind debug macros or dedicated debug code paths.
- Initialize logging explicitly in test or binding-only entrypoints when no main flow exists.

## Examples
- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/source/module/router/Router.cc`
- `src/operation/iCTS/source/data_manager/config/JsonParser.cc`
- `src/operation/iCTS/test/TreeBuilderTest.cc`

## Avoid
- Logging generic "failed" without the object that failed.
- Spamming per-pin logs in production paths.
- Writing a second ad-hoc logging format inside CTS.
