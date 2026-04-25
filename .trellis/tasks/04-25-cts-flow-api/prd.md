# 打通 CTS flow 接口

## Goal

Complete the iCTS flow orchestration layer so clock nets loaded by `CTSAPI::readData()` can be synthesized through `FlowManager`, with branch-level handling for hard macro sinks and register/non-macro sinks before invoking the existing synthesis implementation.

## What I Already Know

* `src/operation/iCTS/source/flow/FlowManager.hh` and `.cc` currently contain only an empty manager shell.
* `src/operation/iCTS/api/CTSAPI.cc` currently calls `readData()` in `runCTS()` but leaves the main flow disabled.
* `Wrapper::read()` resolves each configured/STA-discovered clock net into `Clock::clock_source` and `Clock::loads`.
* `ClockSynthesis::build(Pin*, sinks)` already materializes H-tree objects and creates a source-to-H-tree-root net after H-tree construction.
* `ClockSynthesis::build(Clock&)` owns inserted CTS objects via `Clock::adoptInsertedCtsOwnership`, but FlowManager needs branch-level ownership aggregation when it inserts top branch buffers before synthesis.
* iDB cell masters expose `is_block()`, and CTS-side metadata represents those hard macro instances as `InstType::kMacroBlock` with `Inst::is_macro_block()`.

## Requirements

* Add a real `FlowManager` interface that can run CTS synthesis for all clocks in `Design`.
* Connect `CTSAPI` flow so `runCTS()` performs `readData()` followed by the CTS flow stage.
* In FlowManager, split each clock's sinks into:
  * hard macro sinks: sink pin belongs to an instance backed by a block-class cell master;
  * regular sinks: all other valid sink pins.
* For each non-empty branch, build the structure conceptually as:
  * clock source -> macro branch root buffer -> macro sinks
  * clock source -> regular branch root buffer -> regular sinks
* The branch root buffer output must be passed into synthesis as the branch source. The net from original clock source to the branch root buffer input is owned by the clock result.
* H-tree synthesis should export a recommended root driver sizing after selecting the H-tree pattern. The initial strategy is to use the selected load-side buffer sizing from the H-tree result.
* FlowManager branch root buffers are the physical root drivers for each branch. When a branch runs H-tree synthesis, the branch root buffer must inherit the H-tree recommended root driver sizing.
* Special cases without a usable H-tree root sizing recommendation, including direct branches that skip H-tree construction, must fall back to the smallest configured buffer with resolvable ports and output drive capability.
* Handle synthesis-created H-tree root pins correctly: the branch root buffer output connects to the synthesis H-tree root input through synthesis-owned net construction, and the H-tree root output drives the materialized H-tree/leaf network.
* Avoid disconnected or crossed physical CTS nets after a successful flow:
  * original clock source drives only the branch root buffer inputs created for that clock;
  * branch root buffer output drives only its own branch synthesis entry net;
  * macro and regular sinks do not share branch nets after partitioning.
* If a branch has too few sinks for H-tree construction, keep the flow safe and deterministic by creating a valid branch-level connection rather than leaving sink pins on dangling temporary nets.
* Keep implementation and generated documentation free of third-party project identifiers, copied field names, or copied code.
* Keep tests lightweight: verify flow wiring/partition behavior without requiring a real physical CTS run.
* During development, do not run `ecc_dev_tools` checks. After implementation is locally accepted, run the full iCTS check and report any in-scope findings.

## Acceptance Criteria

* `CTSAPI::runCTS()` invokes the CTS flow after `readData()`.
* `CTSAPI` exposes a callable CTS flow entry point.
* `FlowManager` returns a result/summary that distinguishes success, skipped clocks, failed clocks, and branch/sink counts.
* Hard macro sinks are separated in FlowManager based on iCTS/iDB-owned metadata, not external field names.
* Inserted branch root buffers, branch root nets, and synthesis results are retained by the owning `Clock`.
* Branch root buffer sizing follows H-tree recommended root driver sizing when available; otherwise it uses the minimum-drive fallback buffer.
* A mixed macro/register clock results in two branch root buffers and no shared branch sink net between macro and regular sinks.
* A clock with only one sink in a branch does not leave that sink disconnected.
* Unit or flow-level tests compile and cover the interface path without depending on real design data.
* Final iCTS quality check is run only after implementation and local verification are complete.

## Out Of Scope

* Full timing QoR optimization between macro and regular branches.
* Delay balancing between macro and register arrival times.
* Real DEF/GDS writeback of inserted CTS objects unless already supported by existing wrapper APIs.
* New user-facing configuration fields unless the existing buffer list is insufficient for root buffer creation.
* Real physical benchmark execution in tests.

## Technical Notes

* Likely files:
  * `src/operation/iCTS/source/flow/FlowManager.hh`
  * `src/operation/iCTS/source/flow/FlowManager.cc`
  * `src/operation/iCTS/api/CTSAPI.hh`
  * `src/operation/iCTS/api/CTSAPI.cc`
  * `src/operation/iCTS/source/database/design/Inst.hh`
  * `src/operation/iCTS/source/database/io/Wrapper.cc`
  * `src/operation/iCTS/test/flow/...`
* `InstType::kMacroBlock` and `Inst::is_macro_block()` let FlowManager classify hard macro sinks without depending on iDB pointers.
* Root branch buffer creation should reuse configured buffer masters and STA-resolved buffer ports. Minimum-drive fallback can use output cap limit/table-axis max as the drive capability ranking.
* Tests can construct synthetic `Clock`, `Inst`, `Pin`, and `Net` objects directly and validate FlowManager's partition/summary behavior.
