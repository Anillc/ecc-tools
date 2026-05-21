# W7 Baseline · Singleton IResettable Contract

> Generated 2026-05-20 before W7 implementation begins.

## Current state (post-W6)

### ResettableInterface + SingletonRegistry (existed since W4)

- `src/operation/iCTS/source/utils/singleton/ResettableInterface.hh` — `ResettableInterface` (interface) + `SingletonRegistry` (passive recorder)
- `src/operation/iCTS/source/utils/singleton/SingletonRegistry.cc` — registry impl
- `src/operation/iCTS/source/utils/singleton/CMakeLists.txt` — target `icts_source_utils_singleton`

### 7 singletons already inherit ResettableInterface

| Singleton | File | Macro |
|---|---|---|
| Config | `database/config/Config.hh:39` | `CONFIG_INST` |
| Design | `database/design/Design.hh:44` | `DESIGN_INST` |
| IdbBridge | `database/io/IdbBridge.hh:78` | `IDB_BRIDGE_INST` |
| STAAdapter | `database/adapter/sta/STAAdapter.hh:54` | `STA_ADAPTER_INST` |
| FastSTA | `database/adapter/fast_sta/FastSta.hh:60` | `FAST_STA_INST` |
| Flow | `flow/Flow.hh:47` | `FLOW_INST` |
| SchemaWriter | `utils/logger/Schema.hh:71` | `SCHEMA_WRITER_INST` |

All 7 have:
- `auto reset() -> void override`
- `auto singletonName() const -> std::string_view override` returning class name

### Gap before W7

- `SingletonRegistry::registerSingleton` exists but **no singleton self-registers** in `getInst()`. The registry is `passive`.
- `CTSAPI::resetAPI()` manually lists all 7 singletons (api/CTSAPI.cc:79-92). The list **is currently complete** (not missing FAST_STA / STA_ADAPTER as the PRD §3 P8 evidence claimed).
- Docstrings in `ResettableInterface.hh` say the design "intentionally avoids tying singletons to a particular reset ordering" and "production resets are still driven by CTSAPI::resetAPI which lists the singletons in an explicit order".

### What W7 will change

1. Each of 7 singletons' `getInst()` self-registers with the registry on first call (one-time, thread-safe).
2. `CTSAPI::resetAPI()` is simplified to a single call: `SingletonRegistry::getInst().resetAllReversed()`.
3. Docstrings updated to reflect the new "self-registering" model.
4. New test `test/utils/singleton/SingletonRegistryTest.cc` covers:
   - Each registration is idempotent (registerSingleton twice yields one entry)
   - resetAllReversed walks in LIFO order
   - CTSAPI::resetAPI invokes reset on all touched singletons

## grep verification

```
$ grep -rn 'class .*: public ResettableInterface\|class .*: public icts::ResettableInterface' src/operation/iCTS/source/
src/operation/iCTS/source/flow/Flow.hh:47:class Flow : public ResettableInterface
src/operation/iCTS/source/database/design/Design.hh:44:class Design : public ResettableInterface
src/operation/iCTS/source/database/io/IdbBridge.hh:78:class IdbBridge : public ResettableInterface
src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh:60:class FastSTA : public ResettableInterface
src/operation/iCTS/source/database/config/Config.hh:39:class Config : public ResettableInterface
src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:54:class STAAdapter : public ResettableInterface
src/operation/iCTS/source/utils/logger/Schema.hh:71:class SchemaWriter : public icts::ResettableInterface
=> 7 PASS

$ grep -n 'resetAPI' src/operation/iCTS/api/CTSAPI.cc
79:auto CTSAPI::resetAPI() -> void
85:  CONFIG_INST.reset();
86:  DESIGN_INST.reset();
87:  IDB_BRIDGE_INST.reset();
88:  STA_ADAPTER_INST.reset();
89:  FAST_STA_INST.reset();
90:  FLOW_INST.reset();
91:  SCHEMA_WRITER_INST.reset();
=> currently manual 7-line explicit list
```
