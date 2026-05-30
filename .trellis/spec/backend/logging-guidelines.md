# Logging Guidelines

> How logging is done in this project.

---

## Overview

<!--
Document your project's logging conventions here.

Questions to answer:
- What logging library do you use?
- What are the log levels and when to use each?
- What should be logged?
- What should NOT be logged (PII, secrets)?
-->

(To be filled by the team)

---

## Log Levels

<!-- When to use each level: debug, info, warn, error -->

(To be filled by the team)

---

## Structured Logging

<!-- Log format, required fields -->

### Convention: Preserve the real call site in helper-generated logs

**What**: When a logging helper or stage wrapper emits logs on behalf of caller code, the log source location should still identify the caller, not the helper header.

**Why**: Header-defined helpers and inline wrappers otherwise collapse many unrelated stage logs onto the helper file/line, which makes trace review and failure triage slower.

**Example**:
```cpp
#include <source_location>

inline void logStageInfo(const std::source_location& location, const std::string& message)
{
  google::LogMessage(location.file_name(), static_cast<int>(location.line()), google::GLOG_INFO).stream() << message;
}

template <typename Func>
auto runStage(std::string stage, Func&& func, StageLogOptions options = {},
              std::source_location location = std::source_location::current()) -> bool;
```

**Related**: Helper wrappers around `LOG_*` macros should pass caller location through `std::source_location` or an equivalent mechanism.

---

## What to Log

<!-- Important events to log -->

(To be filled by the team)

---

## What NOT to Log

<!-- Sensitive data, PII, secrets -->

(To be filled by the team)
