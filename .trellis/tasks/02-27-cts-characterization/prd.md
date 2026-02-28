# iCTS Characterization: Reference Flow Reproduction

## Goal

Implement a characterization module in iCTS (`src/operation/iCTS/source/module/characterization/`) that enumerates buffering patterns and obtains timing/power information via iDB, iSTA, and iPA, reproducing the core behavior of an external reference CTS characterization flow.

## Requirements

### R1: Understand Reference Implementation
- Analyze a read-only external CTS characterization implementation to understand:
  - How buffering patterns are enumerated
  - How STA tools are used to obtain timing info (slew, cap, delay, power)
- Understand EDA domain context: CTS and STA business logic

### R2: Implement Characterization Module
- Location: `src/operation/iCTS/source/module/characterization/`
- Read algorithm parameters from `CTSConfigInst` during initialization
- Enumerate buffering patterns similar to the reference
- Build real nets via iDB, real RC trees via iSTA, obtain timing via CTSAPI
- Obtain power via iPA

### R3: Implement CTSAPI Support Functions
- Build temporary real iDB nets (NOT iSTA virtual RC trees)
- Construct iSTA real RC trees from those nets
- Query timing (slew, cap, delay) from iSTA
- Query power from iPA
- **Do NOT create new data structures in CTSAPI** — use existing database objects

### R4: Efficient Pattern Enumeration with Pruning
- Sort buffers by driving capability (max cap from liberty)
- Apply pruning: larger-size buffers drive smaller-size buffers only
- Buffer sorting done at builder initialization time

### R5: Near-Neighbor Redundancy Removal
- Remove near-neighbor redundant entries based on a configurable percentage threshold
- Default behavior: do NOT remove redundancy (threshold = 0 or disabled)

### R6: Unit Conversion
- `db_unit = CTSWrapperInst.getDbUnit()`
- CTSAPI calls to STA for Cap/Res: use actual layout length (`double`, = dbu_length / db_unit)
- Internal char module: use DBU (`uint64_t`)
- Conversion must be explicit and correct at all boundaries

## Acceptance Criteria
- [ ] Characterization module compiles and integrates into iCTS build
- [ ] Buffering patterns are correctly enumerated with pruning
- [ ] Real nets are constructed via iDB (not virtual RC trees)
- [ ] Timing (slew, cap, delay) obtained from iSTA via real RC trees
- [ ] Power obtained from iPA
- [ ] Unit conversion (DBU ↔ layout length) is correct
- [ ] No new data structures added to CTSAPI
- [ ] Near-neighbor redundancy removal is configurable (default: off)
- [ ] No external project names in any source code
- [ ] Code follows iCTS naming conventions and coding standards

## Technical Notes
- Reference materials are read-only external sources
- Must use iDB real nets, not iSTA virtual RC trees, because virtual RC trees cannot model complex buffering timing arcs
- Buffer size ordering derived from liberty max_cap values
