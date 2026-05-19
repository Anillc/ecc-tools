# Design: CTS Code Structure Optimization Leftovers

## Scope

The remediation scope is the current set of iCTS `.cc` and `.hh` files over 600 lines, as reported by:

```bash
find src/operation/iCTS -type f \( -name '*.cc' -o -name '*.hh' \) -print0 | xargs -0 wc -l | awk '$1 > 600 && $2 != "total" {print $1, $2}' | sort -nr
```

## Design Principles

- Preserve behavior by moving cohesive helper groups into sibling files under the same responsibility directory.
- Prefer narrow semantic files over generic utility dumps.
- Keep stage root directories as facades, matching the existing flow pattern where `Evaluation.cc`, `Report.cc`, `Topology.cc`, and `Instantiation.cc` delegate to subdirectory modules.
- Keep public headers stable unless a split requires declarations to move into a narrowly named internal header.
- Keep generated or externally sensitive behavior unchanged; this is structure cleanup, not an algorithm pass.
- Update local CMake wiring with each source split.
- Avoid changing pre-existing edits unless they directly block the cleanup or validation.

## Split Strategy

Use the smallest practical split for each oversized file:

- For source modules, split along existing internal responsibilities such as validation, reporting, route construction, parsing, conversion, scoring, or helper calculations.
- For tests, move fixture/support helpers into `support/` or nearby common files, leaving test cases focused and short.
- For headers, split support types or helper declarations into sibling headers only when implementation files need them.
- For `OptimizationSolver.cc`, preserve the optimization public/internal API and move scalable/exact helper groups only if needed to stay below 600 lines.

## Architecture Closure Strategy

- Keep `source/flow/synthesis/htree/HTree.hh/.cc` as the H-tree facade. Move root-level analytical selection helpers into `analytical_solver/`, selected-solution bridge helpers into `solution/`, compensation helpers into `compensation/`, constraint helpers into `constraint/`, and depth-summary helpers into `plan/`.
- Replace root-level H-tree `HTreeInternal.hh` with narrow subdirectory headers.
- Keep `source/flow/optimization/Optimization.hh/.cc` as the optimization facade. Move internal implementation to `options/`, `model/`, `preparation/`, `candidate/`, `state/`, `solver/`, `mutation/`, and `report/`.
- Replace the flat `OptimizationInternal.hh` fan-out with narrow subdirectory headers.
- Prefer subdirectory CMake targets when moving files into new optimization submodules; keep dependencies target-based.

## Compatibility

- No new user-facing config fields.
- No external module API changes.
- No exception-based control flow.
- Existing CTest names and default registration should remain stable.
- Any manual/real-tech tests touched by a split should keep their existing opt-in or skip behavior.

## Validation

Run fast validation during implementation:

```bash
ninja -C build <affected-targets>
ctest --test-dir build -N -R icts
```

Run the final requested checker:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

After architecture closure, also run:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```
