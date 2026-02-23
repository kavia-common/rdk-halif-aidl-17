<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Quality/compliance/rdk-logging-system-compliance-rules.md
- Operation: write
- Timestamp: 2026-02-12T10:45:35.197484+00:00
- Restored At: 2026-02-23T05:04:11.252364+00:00
- Task ID: cm219d4578
-->

# RDK Logging System Compliance Rules (syslog-ng)

## Scope and intent

This document summarizes the concrete logging compliance rules defined in the RDK HALIF “Logging System” documentation (syslog-ng based). The intent is to provide a concise, implementation-oriented set of rules that TEVDevice can follow to align its runtime logging with RDK expectations.

The referenced RDK document describes a syslog-ng–based platform logging pipeline and states that wrapper/interface layers should emit logs via the standard POSIX `syslog()` API so syslog-ng can route and filter those messages consistently.

## Mandatory interface: use syslog() (not stdout/stderr)

Modules should send runtime logs to the platform logging system via the POSIX syslog API.

In practice, this means:
1. Modules should use `openlog()` to set a stable `ident` (program name / tag) for the component.
2. Modules should emit messages using `syslog(priority, fmt, ...)` with an appropriate syslog priority.
3. Modules may call `closelog()` when shutting down.

The RDK logging system design explicitly aims to remove the need for a custom logging subsystem in wrapper/interface layers by using syslog-ng as the unified backend.

## Syslog priorities: required severities and when to use them

The RDK documentation aligns logging levels to syslog priorities and defines usage guidance and default enablement. The required policy is:

1. `LOG_CRIT` is always enabled and is reserved for unrecoverable errors that require restart or explicit recovery action.
2. `LOG_ERR` is always enabled and is for runtime failures that represent user-visible faults.
3. `LOG_WARNING` is always enabled and is for recoverable conditions and degraded operation.
4. `LOG_NOTICE` is always enabled and is for system milestones such as initialization complete and start/stop events.
5. `LOG_INFO` is optional and is intended for routine operational information in debug builds.
6. `LOG_DEBUG` is optional and is intended for trace-level diagnostics in engineering builds.

Because syslog priorities are also used by syslog-ng filters, the correctness of severity selection matters. In production, the expectation is that only important, actionable information is logged at runtime.

## Production policy: NOTICE and above

The RDK logging system policy states that production images should log at `LOG_NOTICE` and above by default, while `LOG_INFO` and `LOG_DEBUG` are disabled.

This is a compliance requirement because it ensures production systems avoid excessive log volume and overhead while preserving key milestones and fault visibility.

## Build-time gating: ENABLE_LOG_INFO and ENABLE_LOG_DEBUG

The RDK logging system describes build-time verbosity control via compiler defines that compile out optional log levels. Concretely:

1. `ENABLE_LOG_INFO` controls whether INFO-level logging is present in the compiled binary.
2. `ENABLE_LOG_DEBUG` controls whether DEBUG-level logging is present in the compiled binary.

The intended behavior is compile-time filtering: when these flags are not defined, INFO/DEBUG macros expand to no-ops so there is no runtime overhead in production. The documentation provides the canonical pattern:

- If `ENABLE_LOG_INFO` is defined, `LOG_INFO` calls compile to `syslog(LOG_INFO, ...)`; otherwise they compile to `do {} while (0)`.
- If `ENABLE_LOG_DEBUG` is defined, `LOG_DEBUG` calls compile to `syslog(LOG_DEBUG, ...)` (often with extra context like function and line); otherwise they compile to `do {} while (0)`.

The RDK document gives example build configurations:
1. Production build: no flags required; only CRIT/ERR/WARNING/NOTICE are included by default.
2. Debug build: add `-DENABLE_LOG_INFO`.
3. Engineering build: add `-DENABLE_LOG_INFO -DENABLE_LOG_DEBUG`.

## Naming/tagging conventions for routing and filtering (program())

The RDK logging system relies on syslog-ng routing and filtering based on the syslog “program” name, which syslog-ng exposes as `program()`. The documentation shows filters like `filter f_tuner { program("TUNER_HAL"); };`.

Implications for compliance:
1. Each module should choose a stable and distinctive syslog ident (tag) and set it via `openlog("<TAG>", ...)`.
2. This tag is the primary mechanism used by syslog-ng to route a module’s messages into module-specific destinations.
3. Modules should not attempt to reconfigure syslog-ng routing themselves; routing and filtering is controlled at the system level.

The documentation does not prescribe a single global naming format, but it does demonstrate module-specific uppercase tags (for example `TUNER_HAL`) intended to be used consistently across builds so syslog-ng configuration can remain stable.

## Message formatting conventions (context on DEBUG logs)

The RDK document includes an example where DEBUG logs include function name and line number, for example:

- `syslog(LOG_DEBUG, "[%s:%d] " fmt, __func__, __LINE__, ...)`

This is presented as an illustrative macro approach rather than an explicit must, but it is a concrete pattern used to make DEBUG logs actionable without increasing production log verbosity. For compliance with the spirit of the logging system, DEBUG logs should include enough context to be useful for engineering diagnosis (function/line or equivalent).

## Runtime filtering responsibility (syslog-ng configuration)

The RDK documentation shows syslog-ng examples where production routing filters by level range:

- production filter example: `level(notice..emerg)`
- debug/engineering filter example: `level(debug..emerg)`

Compliance rules:
1. Modules emit logs with correct syslog priorities.
2. Modules do not change syslog-ng configuration directly; any routing and filtering changes are system-level decisions, although they may be overridden during development/engineering builds.

## Integration guidance for third-party logging systems

For third-party HALs or external code that uses `printf()` or other logging subsystems, the RDK logging system recommends “simple redirection” rather than modifying third-party code. Examples given include capturing `printf` output, redirecting stderr, or using syslog-ng file/program sources to ingest external logs.

The compliance intent is to avoid invasive changes to third-party code while still achieving unified system visibility via syslog-ng.

## System start ordering requirement (syslog-ng availability)

The documentation states syslog-ng must be started by systemd at the `early` target before vendor layer/component initialization so that logs from wrapper layers are captured from the earliest point in startup.

While TEVDevice may not control platform systemd ordering, it should assume syslog-ng is available early, and it should avoid fallback logging paths that bypass syslog unless explicitly required for bring-up.

## Practical checklist for applying these rules to TEVDevice

To apply the RDK logging system compliance rules to TEVDevice:

1. TEVDevice should emit runtime logs via `syslog()` rather than writing to `stdout`/`stderr`.
2. TEVDevice should set a stable syslog ident via `openlog()` so syslog-ng can route messages using `program()`. This tag should remain stable across builds so syslog-ng filters can be predictable.
3. TEVDevice should treat `LOG_NOTICE` as the “production milestone” level and use it for lifecycle milestones (initialization complete, start/stop).
4. TEVDevice should treat `LOG_WARNING`, `LOG_ERR`, and `LOG_CRIT` as always-on and reserved for conditions matching the RDK definitions.
5. TEVDevice should gate `LOG_INFO` and `LOG_DEBUG` behind `ENABLE_LOG_INFO` and `ENABLE_LOG_DEBUG` so these logs compile out of production.
6. TEVDevice should not implement a runtime “extended logging” toggle that reclassifies severity. Instead, build-time gating should control verbosity, and runtime routing should be left to syslog-ng configuration.

## References

1. In-repo RDK HALIF documentation: `rdk-halif-aidl-17/docs/vsi/filesystem/current/logging_system.md`
2. Published docs page (same content as the in-repo markdown): https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/vsi/filesystem/current/logging_system/
