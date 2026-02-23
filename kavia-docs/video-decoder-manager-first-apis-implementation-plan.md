<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/Other/video-decoder-manager-first-apis-implementation-plan.md
- Operation: write
- Timestamp: 2026-02-13T06:51:44.284621+00:00
- Restored At: 2026-02-23T05:04:11.252417+00:00
- Task ID: cm219d4578
-->

# Video Decoder Manager: First APIs Implementation Plan (getVideoDecoderIds, getSupportedOperationalModes, getVideoDecoder)

## Purpose and scope

This document provides a detailed, start-to-finish implementation plan for the first set of Video Decoder Manager APIs:

- `IVideoDecoderManager.getVideoDecoderIds()`
- `IVideoDecoderManager.getSupportedOperationalModes()`
- `IVideoDecoderManager.getVideoDecoder(IVideoDecoder.Id videoDecoderId)`

The plan is grounded in the repository’s existing AIDL definitions and the Video Decoder HAL pipeline/design documentation. It focuses on implementing the service-side behavior of the manager and its interaction with decoder resource instances, without covering the full decoder controller pipeline (for example, `open()`, `decodeBuffer()`, `start()`) beyond what is necessary to correctly expose and manage decoder instances.

## References in this repository

This plan is based on the following existing sources:

- AIDL manager API definition:
  - `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderManager.aidl`
- AIDL resource instance interface (decoder instance) and ID parcelable:
  - `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`
- Operational modes enum:
  - `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/OperationalMode.aidl`
- HAL pipeline / behavior documentation and requirements:
  - `rdk-halif-aidl-17/docs/halif/video_decoder/current/video_decoder.md`
- Reference class-design skeleton (useful to align structure and initialization strategy):
  - `rdk-halif-aidl-17/videodecoderclassdesign/src/videodecoder/include/rdk/hal/videodecoder/VideoDecoderManager.h`
  - `rdk-halif-aidl-17/videodecoderclassdesign/src/videodecoder/src/VideoDecoderManager.cpp`

## What the AIDL requires (contract summary)

### getVideoDecoderIds()

From `IVideoDecoderManager.aidl`:

- Returns: `IVideoDecoder.Id[]`
- Purpose: “Gets the platform list of Video Decoder IDs.”
- Error model: no explicit error codes; should return a list (possibly empty if platform has no decoders, though typical platforms have >= 1).

From `video_decoder.md` (“Product Customization” and “Resource Management”):

- Should return one `IVideoDecoder.Id` per decoder resource supported by the vendor layer.
- Typically IDs start at 0 and increment by 1.

### getSupportedOperationalModes()

From `IVideoDecoderManager.aidl`:

- Returns: `OperationalMode[]` (one or more values)
- Must not depend on decoder state; can be called any time.
- The returned value is “not allowed to change between calls.”
- Platform must support either tunnelled or non-tunnelled; both are not required.
- Each decoder resource must share the same supported operational mode set as all other decoder resources.
- Graphics texture support is optional.

From `video_decoder.md` (“Operational Modes” section):

- Manager must return all operational modes supported by the video decoders in the system.
- Tunnelled and non-tunnelled cannot be used at the same time, but the platform may support both as options.
- `OperationalMode` is a bitwise-friendly enum; however the AIDL returns an array of enum values, so the service should return the set of supported flags as distinct array entries.

From `OperationalMode.aidl`:

- Values:
  - `TUNNELLED = 1 << 0`
  - `NON_TUNNELLED = 1 << 1`
  - `GRAPHICS_TEXTURE = 1 << 2`

### getVideoDecoder(IVideoDecoder.Id)

From `IVideoDecoderManager.aidl`:

- Input: `IVideoDecoder.Id videoDecoderId`
- Returns: nullable `IVideoDecoder`
- Returns `null` if the ID is invalid.

From `video_decoder.md` (“Resource Management”):

- Manager provides access to multiple `IVideoDecoder` instances, each representing a resource.
- Many clients may obtain `IVideoDecoder` interfaces, but only one client may `open()` a given decoder instance at a time (enforced by the decoder instance itself, not by the manager).

## Design principle: separate concerns

To implement these APIs cleanly and align with existing design skeletons and documentation:

- The **manager** is responsible for:
  - enumerating decoder resources,
  - caching/declaring supported operational modes (constant after initialization),
  - handing out binder interfaces for decoder instances by ID.

- The **decoder instance** (`IVideoDecoder`) is responsible for:
  - enforcing single-open semantics,
  - tracking its state machine (`CLOSED -> OPENING -> READY -> ...`),
  - managing event listeners.

This separation matches the repo’s class design skeleton (`VideoDecoderManager` and `VideoDecoder`) and the documentation’s “Resource Management” section.

## Implementation plan overview (phases)

### Phase 0: Confirm integration target and runtime context

1. Identify where the vendor/service implementation will live.
   - In RDK-style AIDL HAL repos, there is often:
     - a binder service process providing `IVideoDecoderManager` under `serviceName = "VideoDecoderManager"`,
     - and a Service Manager registration step (described in `video_decoder.md` under “Initialization”).
2. Determine whether this repository already contains:
   - a runnable service binary for video decoder manager, or
   - only AIDL interface definitions and a class-design reference.
3. Select the implementation location:
   - If there is an existing service framework used by other HAL modules, follow that same pattern for Video Decoder Manager.
   - If not, use the class design skeleton as the basis for a future service wrapper, but keep this phase limited to implementing the manager behaviors and interfaces.

Deliverable of Phase 0: a short “integration decision” note in the code change review describing where the binder service implementation is placed and how it will be started (for example by systemd unit `hal-video_decoder_manager.service` as referenced by `video_decoder.md`).

### Phase 1: Implement stable resource discovery for getVideoDecoderIds()

1. Define the source of truth for “how many decoders exist and what their IDs are”.
   - The documentation suggests sequential IDs starting at 0.
   - The plan should support:
     - single decoder (common baseline; matches the class design stub),
     - multiple decoders (for concurrent decode).
2. Implement a “resource registry” in the manager:
   - A simple internal vector of IDs, created once during initialization.
   - Example: `[0, 1, 2]` or `[0]`.
3. Decide initialization strategy:
   - Eager initialization: registry built during service startup.
   - Lazy initialization: registry built on first call to any manager method.
   - The class-design skeleton uses lazy initialization via `ensureInitializedLocked()`; that is acceptable as long as:
     - the returned results stay consistent between calls,
     - initialization is thread-safe.
4. Thread-safety:
   - Manager APIs may be called from multiple binder threads.
   - Use a mutex to protect initialization and the registry.

Acceptance criteria for Phase 1:

- `getVideoDecoderIds()` always returns the same IDs during the lifetime of the service process.
- IDs are unique, stable, and match the instances available via `getVideoDecoder()`.

### Phase 2: Implement constant supported operational modes for getSupportedOperationalModes()

1. Determine platform-supported modes.
   - The documentation requires at least one of:
     - `TUNNELLED`, or
     - `NON_TUNNELLED`.
   - `GRAPHICS_TEXTURE` is optional and may be returned in addition to either tunnelled or non-tunnelled.
2. Decide how supported modes are configured:
   - Compile-time configuration (platform macro / build flag).
   - Runtime configuration file read at service startup.
   - Driver query (if there is a vendor driver API that reports capabilities).
3. Enforce “constant between calls”:
   - Cache the supported modes at initialization and return the cached array.
   - Do not query the driver on every call unless the driver guarantees immutability.
4. Enforce “shared modes across all decoder resources”:
   - The manager should compute modes once and apply to all `IVideoDecoder` instances it provides.
   - If future designs allow per-decoder mode differences, the manager must reject that design or normalize to the intersection; for this plan, follow the doc strictly and keep modes identical for all.
5. Validate return shape:
   - The AIDL returns `OperationalMode[]` and states “one or more”.
   - Ensure the returned array is non-empty.

Acceptance criteria for Phase 2:

- The service returns a non-empty array.
- The returned array remains unchanged across repeated calls.
- If the platform supports both tunnelled and non-tunnelled as options, return both.
- If the platform supports texture mode, include `GRAPHICS_TEXTURE` alongside the base mode(s).

### Phase 3: Implement getVideoDecoder(id) to return a stable instance map

1. Implement the “decoder instance map” keyed by ID:
   - `map<int32_t, shared_ptr<VideoDecoderImpl>>` (or binder interface equivalents).
2. Decide lifetime rules:
   - For simplicity and stability, create decoder objects during manager initialization and keep them alive for the process lifetime.
   - This ensures:
     - calling `getVideoDecoder()` multiple times with the same ID returns the same binder object identity,
     - state tracking for that decoder resource remains consistent.
3. Validate the input ID:
   - AIDL ID type is `IVideoDecoder.Id` parcelable with:
     - `const int UNDEFINED = -1;`
     - `int value;`
   - Treat these as invalid:
     - `null` parcelable (if binder permits null parcelable; in AIDL the param is non-null by default, but defensive checks are still good in service code),
     - value == `UNDEFINED` (-1),
     - value not present in the registry/map.
   - In invalid cases: return `null` as the AIDL specifies.
4. Ensure that the returned decoder instance has access to manager-level configuration:
   - It may need to know supported operational modes and its resource ID.
   - This can be done by:
     - passing the ID and supported modes into the decoder constructor, or
     - injecting a shared “platform config” object.

Acceptance criteria for Phase 3:

- `getVideoDecoder(validId)` returns non-null and is usable for subsequent calls (`getCapabilities()`, `getState()`, `open()`, etc.).
- `getVideoDecoder(invalidId)` returns `null`.
- Multiple calls for the same `validId` return a consistent instance (recommended) or equivalent functional instance (minimum), but must not violate “only 1 open at a time” semantics in the decoder instance.

## Detailed step-by-step implementation sequence (suggested order)

### Step 1: Build the manager’s internal model

Implement a concrete “VideoDecoderManagerImpl” with fields:

- `std::vector<int32_t> m_ids;`
- `std::vector<OperationalMode> m_supportedOperationalModes;`
- `std::unordered_map<int32_t, std::shared_ptr<VideoDecoderImpl>> m_decoders;`
- `bool m_initialized;`
- `std::mutex m_mutex;`

This structure aligns with the class design skeleton in:

- `videodecoderclassdesign/.../VideoDecoderManager.h`
- `videodecoderclassdesign/.../VideoDecoderManager.cpp`

### Step 2: Define initialization routine

Implement something like `ensureInitializedLocked()`:

- Populate `m_supportedOperationalModes` once.
- Populate `m_ids` once.
- Create decoder instances and populate `m_decoders`.

The reference skeleton creates one decoder with ID 0 and sets supported modes to `NON_TUNNELLED`. That is the minimum viable baseline.

### Step 3: Implement getVideoDecoderIds()

- Lock mutex.
- Call `ensureInitializedLocked()`.
- Return array of `IVideoDecoder.Id` parcelables, one per `m_ids`.

Be careful to produce the AIDL ID parcelable type, not a raw `int`.

### Step 4: Implement getSupportedOperationalModes()

- Lock mutex.
- Call `ensureInitializedLocked()`.
- Return the cached `m_supportedOperationalModes` as the AIDL enum array.

### Step 5: Implement getVideoDecoder(id)

- Lock mutex.
- Call `ensureInitializedLocked()`.
- Validate:
  - `id.value != -1`,
  - `id.value` exists in `m_decoders`.
- Return the binder interface for that decoder instance or `null`.

### Step 6: Add contract-oriented logging and diagnostics

Even though AIDL comments do not mandate logging, it is valuable for integration/debugging:

- Log at least:
  - initialization completion (IDs, supported operational modes),
  - `getVideoDecoder()` invalid ID attempts (at debug level),
  - any driver query failures when computing supported modes.

This helps with black-box testing and aligns with the broader vDevice / control-plane emphasis on observability.

## API behavior matrix (for testing and review)

| API | Input | Output | Notes |
|---|---|---|---|
| getVideoDecoderIds | none | non-null array of `IVideoDecoder.Id` | Stable across calls; usually `[0..N-1]` |
| getSupportedOperationalModes | none | non-empty `OperationalMode[]` | Stable across calls; includes at least `TUNNELLED` or `NON_TUNNELLED` |
| getVideoDecoder | valid ID from `getVideoDecoderIds()` | non-null `IVideoDecoder` | Should be stable instance; enforces single-open internally |
| getVideoDecoder | ID = `UNDEFINED (-1)` | null | As per AIDL comment |
| getVideoDecoder | unknown ID | null | As per AIDL comment |

## Alignment with Video Decoder pipeline documentation

The manager APIs implemented here are explicitly required by the Video Decoder HAL “Resource Management” and “Product Customization” sections in:

- `docs/halif/video_decoder/current/video_decoder.md`

In particular:

- “HAL.VIDEODECODER.9” requires reporting number of instances supported and their capabilities.
  - The first part of this is satisfied by `getVideoDecoderIds()` returning the resource set.
  - The second part is satisfied by clients calling `getVideoDecoder(id).getCapabilities()`. This plan ensures `getVideoDecoder()` returns a functional instance to support that flow.
- “Operational Modes” requires `getSupportedOperationalModes()` to reflect the system’s supported mode set and to be constant between calls.

## Suggested minimal implementation milestone (MVP)

For first delivery and for early integration with a GStreamer-based client element:

- Support exactly one decoder resource:
  - `getVideoDecoderIds()` returns `[ { value: 0 } ]`
- Support exactly one operational mode:
  - return `[ OperationalMode.NON_TUNNELLED ]`
- `getVideoDecoder({value:0})` returns the single decoder.
- `getVideoDecoder({value:-1})` returns null.
- `getVideoDecoder({value:999})` returns null.

This matches the class-design stub behavior and provides a predictable baseline for client integration.

## Follow-on work (not part of this first API set)

After these manager APIs are implemented and stable, the next work items that naturally follow from the pipeline documentation are:

- Implement `IVideoDecoder.getCapabilities()` to reflect codec and secure support correctly.
- Implement property model, including `OPERATIONAL_MODE` property semantics described in the Operational Modes section.
- Implement single-open behavior and controller lifecycle (`open()`, `close()`, `start()`, `decodeBuffer()`, `flush()`, `signalEOS()`) consistent with the state diagrams and callback requirements.

These are intentionally out of scope for this document, but the manager implementation described here should be designed to not block these follow-on steps.

## Instructions for future agent

If you need to tie this plan to a concrete service binary and system startup, search for patterns used by other HAL modules for Service Manager registration and binder threadpool setup, then apply the same pattern to `IVideoDecoderManager.serviceName = "VideoDecoderManager"`, consistent with the “Initialization” section of `docs/halif/video_decoder/current/video_decoder.md`.
