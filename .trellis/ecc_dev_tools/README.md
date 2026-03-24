# ecc_dev_tools

A local C++ quality checker for `src/operation/iCTS`.

## Checks

- `format`
  - `clang-format` consistency check
  - `--fix` only fixes formatting
- `tidy`
  - `clang-tidy` check families
  - Deep mode also runs analyzer / clang frontend / native fallback
  - Compiler warnings are reported under `clang-diagnostic`
- `headers`
  - Standalone header compilation
  - Include-first header compilation
  - Missing direct target dependency checks
- `cmake`
  - Target cycles
  - Redundant direct links
  - `PRIVATE` / `PUBLIC` / `INTERFACE` visibility checks
- `iwyu`
  - Missing include
  - Unnecessary include
  - Missing forward declaration

## Quick Start

```bash
# Environment check
python3 ./.trellis/ecc_dev_tools/check.py doctor

# Run the touched path first
python3 ./.trellis/ecc_dev_tools/check.py check --path <touched-path>

# If you changed public headers or CMake, also run structure checks
python3 ./.trellis/ecc_dev_tools/check.py check --path <touched-path> --preset structure

# Run full iCTS at the end
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS --no-fail-on-findings --quiet
```

## Common Commands

```bash
# Default: format + tidy + headers + cmake + iwyu
python3 ./.trellis/ecc_dev_tools/check.py check --path <path>

# Quality only
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --preset quality

# Structure only
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --preset structure

# Tidy only
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --preset tidy-only

# IWYU only
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --preset iwyu-only
```

## Tidy Modes

```bash
# Recommended default
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --tidy-mode deep --pass-plan complete

# Naming cleanup only
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --tidy-mode naming
```

Deep mode currently includes:
- `bugprone-*`
- `modernize-*`
- `performance-*`
- `readability-*`
- `misc-*`
- `cppcoreguidelines-*`
- `readability-identifier-naming`

`complete` pass plan also enables:
- `clang-analyzer-*`
- Clang frontend with `-Wall -Wextra -Wconversion -Wsign-conversion`
- On-demand native compiler fallback

## Output Formats

```bash
# Default text output
python3 ./.trellis/ecc_dev_tools/check.py check --path <path>

# JSON for scripts
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --output-format json --quiet --no-fail-on-findings

# Compiler-style output for editor problem lists
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --output-format compiler
```

Notes:
- `text`: full summary with notes
- `json`: includes `summary`, `notes`, and `findings`
- `compiler`: in-scope findings only, no notes or summary

## Exit Codes

- `0`
  - No in-scope findings, or `--no-fail-on-findings` is set
- `1`
  - In-scope findings exist
- `2`
  - Argument errors, missing required tools, build metadata refresh failure, or other runtime errors

Notes:
- `out_of_scope` findings do not affect the exit code
- Automation usually pairs JSON output with `--no-fail-on-findings`

## Build Metadata

These checks require build metadata:
- `tidy`
- `headers`
- `cmake`
- `iwyu`

The tool reuses and refreshes these files as needed:
- `build/compile_commands.json`
- `build/.cmake/api/v1/reply/`
- `build/.ecc_dev_tools/cmake_trace.json`

`cmake` checks no longer rely on hand-parsing `CMakeLists.txt` text. They use:
- CMake File API
- CMake trace JSON

If trace parsing skips generator-expression link items, the checker reports that in `cmake` notes.

## Suppressions

File:
- `.trellis/ecc_dev_tools/suppressions.jsonl`

Use it for:
- Confirmed false positives
- Known tool conflicts

Avoid:
- Hiding real code issues with suppressions

Show suppressed findings:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path <path> --show-suppressed
```

## Tool Dependencies

Required:
- `cmake`
- `ninja`
- `clang-format`
- `clang-tidy`
- `clang++`
- `g++`

Optional:
- `clang-scan-deps`
- `include-what-you-use`

Notes:
- Missing optional tools do not fail the command; the related capability is skipped or downgraded
- Binaries are auto-selected from the newest available version in `PATH`
