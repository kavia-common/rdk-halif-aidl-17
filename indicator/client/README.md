# IndicatorClient (Stable AIDL client)

This folder provides a small C++ client adapter for the Stable AIDL indicator interfaces:

- `com.rdk.hal.indicator.IIndicatorManager`
- `com.rdk.hal.indicator.IIndicator`

## What it does

`rdk::hal::indicator::IndicatorClient` implements:

- service discovery of `IIndicatorManager` via the Service Manager
- enumeration of available indicator IDs (`getIndicatorIds()`)
- acquisition of an `IIndicator` per-id handle (`getIndicator(id)`)
- capability caching (`getCapabilities()`)
- capability-gated `setState(state)` (validates state is in `supportedStates`)
- robust binder status handling and explicit error returns (no exceptions)

## Build & integration notes

This repository's CMake `compile_aidl()` generates headers into:

- `${AIDL_GEN_DIR}/h/...`

When building the client in-tree, `indicator/client/CMakeLists.txt` adds `${AIDL_GEN_DIR}/h` to the include path (if `AIDL_GEN_DIR` is defined).

You still need to link the platform's binder NDK library (commonly `binder_ndk`) in your final consumer binary.

## Usage (high level)

1. `connect()`
2. `listIndicatorIds()`
3. `selectIndicator(id)`
4. `getState()` / `setState("ACTIVE")`

See `IndicatorClient.h` for the full API.
