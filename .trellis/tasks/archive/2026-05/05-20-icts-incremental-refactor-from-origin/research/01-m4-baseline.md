# M4 Baseline · IResettable + SingletonRegistry

> Captured before M4 implementation. Baseline HEAD = `cb97062f1`.

## Discrepancy with PRD assumption

PRD §3 M4 and the dispatch prompt both assert "origin/cts_refactor 已有
`utils/singleton/ResettableInterface.hh`（W4 时引入；本 task 不要重新定义，复用它）".
**This is incorrect for the actual baseline**:

- `find src/operation/iCTS -name 'ResettableInterface*'` returns nothing.
- `find src/operation/iCTS -type d -name singleton` returns nothing.
- `grep -rn 'ResettableInterface' src/operation/iCTS` returns nothing.

The `ResettableInterface` infrastructure was on the local W0~W9 chain (commit
`915468e2a`) which was reset away before this task. Origin's
`597cc31b8` "close CTS code normalization work" path did NOT introduce the
interface.

**Adaptation**: M4 will create both `ResettableInterface.hh` and
`SingletonRegistry.{hh,cc}` from scratch (following the archived W4/W7 design
verbatim where possible).

## Singleton inventory (current baseline)

PRD lists 7 singletons but the actual class names differ from PRD wording:

| PRD wording | Actual class / file | Macro | Has `reset()`? |
|---|---|---|---|
| `Flow` | `class Flow` in `flow/Flow.hh:54` | `FLOW_INST` | yes |
| `Config` | `class Config` in `database/config/Config.hh:37` | `CONFIG_INST` | yes (inline) |
| `Design` | `class Design` in `database/design/Design.hh:42` | `DESIGN_INST` | yes |
| `IdbBridge` | **`class Wrapper`** in `database/io/Wrapper.hh:74` | `WRAPPER_INST` | yes (inline) |
| `STAAdapter` | `class STAAdapter` in `database/adapter/sta/STAAdapter.hh:47` | `STA_ADAPTER_INST` | **no** |
| `FastSTA` | `class FastSTA` in `database/adapter/fast_sta/FastSta.hh:211` | `FAST_STA_INST` | **no** (has `clear()`) |
| `SchemaWriter` | `class SchemaWriter` in `utils/logger/Schema.hh:69` (`icts::schema` ns) | `SCHEMA_WRITER_INST` | yes |

**Adaptation**: treat origin's `Wrapper` (not the PRD's "IdbBridge") as the
canonical singleton. Add a minimal `reset()` to STAAdapter (no instance state
to clear; document via singletonName()) and a `reset()` to FastSTA that
delegates to existing `clear()`.

## CTSAPI::resetAPI baseline

```cpp
auto CTSAPI::resetAPI() -> void
{
  CONFIG_INST.reset();
  DESIGN_INST.reset();
  WRAPPER_INST.reset();
  FLOW_INST.reset();
  SCHEMA_WRITER_INST.reset();
}
```

5 manual resets. After M4 will become a single `SingletonRegistry::getInst().resetAllReversed()`.

## CMake & test pattern (for reference)

- Source CMakeLists pattern: `src/operation/iCTS/source/utils/logger/CMakeLists.txt` shows the
  `add_library(icts_source_utils_<name>)` + `target_include_directories` +
  optional `DEBUG_ICTS_*` block.
- `src/operation/iCTS/source/utils/CMakeLists.txt` aggregates subdirs into
  `icts_source_utils` INTERFACE.
- Test CMake helper: `icts_add_test_executable(name SOURCES ... LIBS ...)`
  defined in `test/CMakeLists.txt`.
- Test subdir pattern: `test/utils/graph/CMakeLists.txt` + `test/utils/CMakeLists.txt`
  via `add_subdirectory(...)`.

## Post-M4 verification

### Singletons inheriting ResettableInterface

```
$ grep -rEn 'class .*: public (icts::)?ResettableInterface' src/operation/iCTS/source/
src/operation/iCTS/source/flow/Flow.hh:57:class Flow : public ResettableInterface
src/operation/iCTS/source/database/config/Config.hh:40:class Config : public ResettableInterface
src/operation/iCTS/source/database/io/Wrapper.hh:77:class Wrapper : public ResettableInterface
src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:51:class STAAdapter : public ResettableInterface
src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh:214:class FastSTA : public ResettableInterface
src/operation/iCTS/source/database/design/Design.hh:45:class Design : public ResettableInterface
src/operation/iCTS/source/utils/logger/Schema.hh:72:class SchemaWriter : public icts::ResettableInterface
=> 7 PASS
```

### SingletonRegistry usage sites

```
$ grep -rEn 'SingletonRegistry::getInst' src/operation/iCTS/source/ src/operation/iCTS/api/
api/CTSAPI.cc:80:  SingletonRegistry::getInst().resetAllReversed();
source/database/adapter/fast_sta/FastSta.hh:221:      ...registerSingleton(&instance);
source/database/adapter/sta/STAAdapter.hh:97:      ...registerSingleton(&instance);
source/database/config/Config.hh:47:                    ...registerSingleton(&instance);
source/database/design/Design.hh:52:                    ...registerSingleton(&instance);
source/database/io/Wrapper.hh:84:                       ...registerSingleton(&instance);
source/flow/Flow.hh:64:                                 ...registerSingleton(&instance);
source/utils/logger/Schema.hh:140:                      ...registerSingleton(&instance);
source/utils/singleton/SingletonRegistry.cc:30: ::getInst() definition
=> 7 singletons + CTSAPI + registry impl
```

### CTSAPI::resetAPI after M4

```cpp
auto CTSAPI::resetAPI() -> void
{
  SingletonRegistry::getInst().resetAllReversed();
}
```

5 manual resets -> 1 registry call (line count 7 -> 4 in resetAPI body).

### New file sizes

```
$ wc -l src/operation/iCTS/source/utils/singleton/{ResettableInterface.hh,SingletonRegistry.hh,SingletonRegistry.cc}
  52 ResettableInterface.hh
  69 SingletonRegistry.hh
  83 SingletonRegistry.cc
 204 total
```

### Build + tests

- `bash build.sh` exit 0; iEDA binary linked at 21:38:54 (CST) after final edit.
- `ninja -C build icts_test_utils_singleton` builds clean.
- `./bin/icts_test_utils_singleton`: 5/5 tests PASS
  - RegistrationIsIdempotent
  - RegistrationNullPointerIsGuarded
  - ProductionSingletonsAreRegistered (verifies all 7 singletons land in registry)
  - CustomSingletonResetSweep
  - MultiSweepIsIdempotent
