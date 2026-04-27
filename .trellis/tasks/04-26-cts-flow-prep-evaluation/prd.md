# 拆分 CTS Flow 前期准备与评估模块

## Goal

Refactor the iCTS flow preparation, synthesis, and evaluation path around a final Design-owned netlist and a net-based CTS flow. Temporary algorithm objects remain isolated until success; final writeback and evaluation operate from each clock's committed final membership.

## Design Ownership Contract

* `Design` owns all final `Clock`, `Inst`, `Pin`, and `Net` objects through `std::unique_ptr`.
* `Clock` owns no final topology objects. It keeps only borrowed pointers for clock source, source net, original loads, and this clock's final inst/net membership.
* `Inst`, `Pin`, and `Net` topology links remain borrowed pointer relationships.
* Algorithm results may own temporary `inserted_*` objects while synthesis or H-tree construction is in progress. These objects are committed into `Design` and `Clock` membership only after the algorithm succeeds.
* Failed algorithm results destruct without polluting final `Design` or `Clock` state.
* Do not add task-specific ownership wrappers or change tracking structures such as `Branch*`, `Record*`, `Writeback*`, `DesignObject`, `is_dirty`, `is_new`, graph traversal helpers, or redundant CTS object containers.

## Pin Identity And Lookup

* `Design::makePin(name)` creates a pin with a local pin name only.
* Full-name indexing happens through `Design::indexPin(pin)` only after the final pin-inst relation is set.
* Pin lookup is by full-name string only.
* Do not introduce or keep a `findPin(Pin*)` overload.
* When root-buffer pins are resized, renamed, or rebound, callers must maintain the `Design` full-name pin index consistently.

## Net-Based Flow Contract

* Flow synthesis and H-tree construction are net-based.
* A root `Net` represents `driver pin -> load pins`.
* `ClockSynthesis` and `HTreeBuilder` consume `Net&` as the flow path input, not a standalone loads vector.
* `HTreeBuilder` derives `root_driver_pin`, `root_driver_inst`, and loads from the input net.
* `HTreeBuilder::BuildOptions` must not carry `root_driver_pin` or `root_net` as hidden inputs.
* H-tree root driver sizing is applied directly to the root driver instance after successful H-tree materialization.
* Do not pass `recommended_root_driver_cell_master` back to `FlowManager` as a command. A result may report the actual selected root driver master only as factual/report data if useful.

## ClockSynthesis Requirements

* `ClockSynthesis` consumes `Net& root_net`.
* Optional clustering runs from the input net's loads, then H-tree runs on that net-based view.
* Remove the old overload-heavy `Pin + sinks` and `Pin + Net` facade as the target flow design.
* `source_to_root_net` is not part of the synthesis result. The input net is the root/source net for the synthesis operation.
* Successful synthesis commits temporary `inserted_*` objects into final `Design` ownership and clock membership through the flow/net manager boundary.

## ClockNetManager Responsibilities

`ClockNetManager` owns final `Design` netlist mutation for flow preparation:

* read clock data into final `Design` objects,
* restore each clock source net,
* partition macro and regular sinks,
* insert root buffers with minimum fallback,
* create, connect, and reconnect final nets,
* resize and rename root-buffer pins while preserving `Design` full-name pin indexes,
* connect downstream root nets to root-buffer outputs,
* connect the clock source net to root-buffer inputs,
* commit successful temporary algorithm objects into `Design` ownership and `Clock` final membership.

## FlowManager Responsibilities

`FlowManager` orchestrates but does not own low-level topology mutation:

1. For each clock, split original sinks into macro and regular groups.
2. Ask `ClockNetManager` to insert root buffers with the minimum fallback behavior.
3. Ask `ClockNetManager` to connect downstream root nets.
4. For multi-sink groups, call net-based synthesis on the downstream root net.
5. For single-sink groups, keep direct nets instead of forcing H-tree synthesis.
6. Ask `ClockNetManager` to connect the source net to root-buffer inputs after downstream roots are prepared.

`FlowManager` must not interpret H-tree recommended root sizing or issue a second root-driver sizing command based on an algorithm recommendation.

## Evaluation Requirements

* Evaluation is final-membership based.
* Write the final `Design` netlist to iDB before evaluation.
* Refresh STA only after iDB writeback completes.
* Build and install RC trees from the committed final netlist.
* Collect timing and summary data from final `Clock` membership, including source-side nets.
* Evaluation must not mutate `Design` topology.

## API Boundary

* Source code under `src/operation/iCTS/source` must not depend on `ieda_feature::CTSSummary`.
* `CTSAPI` maps source-layer summary data to API/feature-layer summary data at the API boundary.

## Tests And Checks

During implementation, run the lightweight checks that match the changed flow surface:

```bash
git diff --check
cmake --build build --target icts_api icts_test_flow_manager icts_test_flow_htree icts_test_flow_synthesis -j 8
./bin/icts_test_flow_manager
./bin/icts_test_flow_htree
./bin/icts_test_flow_synthesis
```

Do not run `ecc_dev_tools` during implementation. The final full ECC/iCTS validation is reserved for the finish/check stage.

## Acceptance Criteria

* Final CTS objects have one owner: `Design`.
* `Clock` contains only borrowed anchors, original loads, and final inst/net membership.
* Pin creation and full-name indexing follow the local-name then `indexPin()` contract.
* Synthesis and H-tree flow paths consume `Net&`.
* `HTreeBuilder::BuildOptions` has no hidden root driver or root net inputs.
* Root driver sizing is applied during successful H-tree materialization, not interpreted later by `FlowManager`.
* `ClockSynthesis` no longer targets overload-heavy `Pin + sinks` or `Pin + Net` facades.
* `ClockNetManager` owns final Design netlist mutation and successful temporary-object commits.
* `FlowManager` only orchestrates partitioning, root insertion, net-based synthesis, direct single-sink nets, and source-net connection.
* Evaluation writes final membership to iDB, refreshes STA after writeback, installs RC trees, collects timing/summary data, and does not mutate topology.
* No task-specific redundant structures or graph traversal systems are introduced.
* Source-layer iCTS code remains independent of `ieda_feature::CTSSummary`.

## Out Of Scope

* New QoR optimization.
* Signoff detailed routing output.
* New iDB/iSTA public interfaces beyond what writeback, STA refresh, RC tree installation, and evaluation require.
* Running `ecc_dev_tools` in this implementation pass.
