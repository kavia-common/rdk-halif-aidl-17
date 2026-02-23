<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder_design/VideoDecoder_design doc.md
- Operation: write
- Timestamp: 2026-02-10T12:59:39.889791+00:00
- Restored At: 2026-02-23T05:04:11.251606+00:00
- Task ID: cm219d4578
-->

# Video Decoder Design (HALIF AIDL) — Architecture, Flows, and Buffer Lifecycle

## Executive summary

This document describes a practical design view of the HALIF AIDL **Video Decoder** subsystem and its integration with the **AV Buffer** subsystem. It is grounded in the upstream HALIF documentation and the AIDL interface contracts present in this workspace, and it is intended to help implementers and integrators reason about:

How the platform exposes decoder resources through `IVideoDecoderManager` and per-resource `IVideoDecoder` instances.

How a decode *session* is created via `IVideoDecoder.open(...)`, controlled and fed via `IVideoDecoderController`, and observed via event and controller listener callbacks.

How encoded input data and decoded output frame buffers are exchanged across component boundaries using globally unique AV Buffer handles allocated/freed via `IAVBuffer`.

How the state machine (`CLOSED/OPENING/READY/STARTING/STARTED/FLUSHING/STOPPING/CLOSING`) constrains control flow, defines safe transitions, and acts as a cleanup boundary for buffers.

Which “normal” backpressure and error paths must be handled (for example, `decodeBuffer()` returning `false`, `destroyPool()` failing with `NOT_EMPTY`, and invalid handle usage).

The goal is not to restate the entire upstream spec verbatim, but to present an implementable mental model of **architecture**, **data/control flow**, **buffer lifecycle**, and **robust error handling**.

## Scope

This document focuses on the HALIF AIDL surfaces used by a typical client pipeline (for example, middleware) to drive video decoding:

Video Decoder APIs:
- `IVideoDecoderManager`
- `IVideoDecoder`
- `IVideoDecoderController`
- `IVideoDecoderEventListener`
- `IVideoDecoderControllerListener`

AV Buffer APIs:
- `IAVBuffer`
- `Pool`
- `IAVBufferSpaceListener`
- Heap/pool metrics (for observability and sizing feedback)

Out of scope:
- Vendor-specific implementation details below the AIDL boundary (hardware blocks, codecs, kernel drivers, etc.).
- Video Sink / Plane Control specifics (except where tunnelled mode implies linkage).
- A full DRM/CDM design; only secure vs non-secure buffer/decoder constraints relevant to the HALIF APIs are discussed.

## Architecture and components

### High-level architecture

At a system level, the design is two cooperating services plus client-side logic:

1. The **Video Decoder service family** provides discovery (`IVideoDecoderManager`), per-resource operations (`IVideoDecoder`), and per-session control/data plane (`IVideoDecoderController`).
2. The **AV Buffer service** (`IAVBuffer`) provides memory heaps (secure / non-secure), pools, and **globally unique** `long` buffer handles that can be passed between services and freed by different components.
3. The **client pipeline** allocates coded (compressed) frame buffers, fills them, submits them to the decoder, receives decoded outputs (non-tunnelled), and frees output buffers when downstream is done.

A key design principle is that **buffer ownership can transfer across components** because AV Buffer handles are globally unique and free-able via `IAVBuffer.free(handle)` regardless of which process allocated them.

### Component breakdown

#### IVideoDecoderManager (service)

`IVideoDecoderManager` is the entry point for discovering platform decoder resources and the platform-level supported operational modes.

Key responsibilities and calls:
- `getVideoDecoderIds()` returns `IVideoDecoder.Id[]` for available decoder resources.
- `getSupportedOperationalModes()` returns `OperationalMode[]` (one or more). The contract states the returned value does not change between calls.
- `getVideoDecoder(videoDecoderId)` returns an `IVideoDecoder` instance or `null` if the ID is invalid.

Design implications:
- Clients should treat the ID list as authoritative for the boot/session.
- Operational modes are platform-wide and consistent across all decoder resources.

#### IVideoDecoder (resource instance)

`IVideoDecoder` represents a single decoder resource and is the boundary for:
- Capability queries: `getCapabilities()`.
- Properties: `getProperty(...)`, `getPropertyMulti(...)`.
- State observation: `getState()`.
- Session creation: `open(codec, secure, controllerListener)`.
- Session teardown: `close(controller)`.
- Eventing: `registerEventListener(...)` and `unregisterEventListener(...)`.

Session creation:
- `open(codec, secure, controllerListener)` is only allowed when the resource is in `CLOSED`.
- On success, the resource transitions `CLOSED -> OPENING -> READY`.
- The method returns an `IVideoDecoderController` used for decode and session control. It returns `null` if codec or secure mode is not supported.

Crash safety:
- If the client process that opened the `IVideoDecoderController` crashes, the decoder service implicitly performs `stop()` and `close()` to clean up. This is critical for avoiding leaked coded input buffers.

#### IVideoDecoderController (session control + data plane)

`IVideoDecoderController` is the per-session interface returned by `open(...)`.

Lifecycle control:
- `start()` transitions `READY -> STARTING -> STARTED`.
- `stop()` transitions `STARTED -> STOPPING -> READY` and behaves like a flush (it frees queued inputs).
- `flush(reset)` runs while `STARTED`, frees queued inputs, returns pending output frames to the output pool, and optionally resets internal state fully when `reset=true`.
- `signalEOS()` declares end-of-stream; decoder must eventually output metadata with EOS set.
- `signalDiscontinuity()` marks PTS discontinuity boundaries for subsequent frames.
- `parseCodecSpecificData(format, codecData)` provides out-of-band initialization data; must be called in `STARTED` and before `decodeBuffer()` when required.

Data plane:
- `decodeBuffer(nsPresentationTime, bufferHandle)` submits one coded frame by AV Buffer handle.
- Returns `true` on acceptance, or `false` if the decoder-side “decode buffer is full” (a normal backpressure condition).

Ownership rule (normative):
- If `decodeBuffer(...)` succeeds (`true`), the caller must not modify or free the handle after submission. The decoder must free it once consumed (it is “automatically released and returned to the AV Buffer Manager”).

#### IVideoDecoderEventListener (event callbacks)

`IVideoDecoderEventListener` is a oneway callback interface for:
- `onStateChanged(oldState, newState)` for lifecycle state transitions.
- `onDecodeError(errorCode, vendorErrorCode)` for runtime decode failures.

Design implications:
- Client control flow should treat asynchronous state transitions as authoritative and be tolerant of transitory states.
- Decode-time errors are not necessarily synchronous call failures; they can arrive out-of-band.

#### IVideoDecoderControllerListener (output callbacks)

`IVideoDecoderControllerListener` is a oneway callback interface for:
- `onFrameOutput(nsPresentationTime, frameBufferHandle, metadata)` for decoded output and/or metadata.
- `onUserDataOutput(nsPresentationTime, userData)` for picture user data in presentation order.

Metadata rules (key for throughput):
- `metadata` must be non-null on the first frame after `start()` or `flush()`, and when metadata changes.
- It may be null when unchanged to reduce CPU load.

Tunnelled vs non-tunnelled:
- In non-tunnelled mode, `frameBufferHandle` is a decoded 2D frame buffer handle.
- In tunnelled mode, decoded frames are not returned to the client. When metadata must still be delivered, `frameBufferHandle = -1` indicates no frame handle is delivered.

#### IAVBuffer (service), pools, and handles

`IAVBuffer` provides:
- Two heap types: secure and non-secure (visible via `getHeapMetrics(secureHeap)`).
- Video and audio pools:
  - `createVideoPool(secureHeap, videoDecoderId, listener)`
  - `createAudioPool(secureHeap, audioDecoderId, listener)`
- Pool destruction:
  - `destroyPool(poolHandle)` requires the pool to be empty; otherwise returns service-specific error `HALError::NOT_EMPTY`.
- Allocations:
  - `alloc(poolHandle, size)` returns a `long` handle or throws service-specific `HALError::OUT_OF_MEMORY`.
  - `trimSize(handle, newSize)` only for the last allocation in a pool.
  - `free(handle)` returns `true`/`false` depending on handle validity.
- Backpressure notification:
  - `notifyWhenSpaceAvailable(poolHandle, size)` paired with `IAVBufferSpaceListener.onSpaceAvailable()`.
- Validation and debug:
  - `isValid(handle)` and `getAllocList(poolHandle)`.

Pool representation:
- `Pool` is a parcelable containing a `byte handle` with `INVALID_POOL = -1`.

Handle representation:
- Buffer allocations are represented by a global `long` handle.
- A critical property is **global uniqueness** across heaps/pools, including vendor-private frame pools, enabling cross-component freeing.

### ASCII sketch: component relationships

```
+----------------------+       +-----------------------+
| Client pipeline      |       | Video Decoder service  |
| (middleware)         |       |                       |
|                      |       |  +-----------------+  |
| +------------------+ |       |  | IVideoDecoder   |  |
| | IVideoDecoderMgr |<--------->  | (resource)     |  |
| +------------------+ |       |  +---+--------+----+  |
|           |          |       |      | open() |       |
|           v          |       |      v        |       |
| +------------------+ |       |  +-----------------+  |
| | IVideoDecoder    | |       |  | IVideoDecoder   |  |
| | (per ID)         | |       |  | Controller      |  |
| +--------+---------+ |       |  +--+----------+---+  |
|          | open()    |       |     | decode() |      |
|          v           |       |     v          v      |
|   +----------------+ |       |  CtrlListener  Event  |
|   | Controller     |<------------------ callbacks ----|
|   +----------------+ |       +-----------------------+
|
|       allocate/free handles
v
+----------------------+
| AV Buffer service    |
| IAVBuffer            |
| - heaps (secure/non) |
| - pools              |
| - alloc/free/notify  |
+----------------------+
```

## State machine and transitions

### Common HAL State enum

The Video Decoder uses the common HAL `State` enum (`com.rdk.hal.State`) with:

`UNKNOWN`, `CLOSED`, `OPENING`, `READY`, `STARTING`, `STARTED`, `FLUSHING`, `STOPPING`, `CLOSING`.

### Primary state transitions (typical success path)

The typical happy path state progression is:

```
CLOSED --open()--> OPENING --> READY --start()--> STARTING --> STARTED
STARTED --flush()--> FLUSHING --> STARTED
STARTED --stop()--> STOPPING --> READY --close()--> CLOSING --> CLOSED
```

Key design points:
- `open()` requires `CLOSED`.
- `start()` requires `READY`.
- Decode-plane calls (`decodeBuffer`, `flush`, `signalEOS`, `signalDiscontinuity`, `parseCodecSpecificData`) require `STARTED`.
- `stop()` requires `STARTED` and returns to `READY`.
- `close()` requires `READY`.
- Transitory states (`OPENING`, `STARTING`, `FLUSHING`, `STOPPING`, `CLOSING`) are visible via `onStateChanged(...)`.

### Buffer-cleanup guarantees tied to transitory states

A critical robustness property is that **control operations force buffer release**:

- `stop()` explicitly states that buffers submitted for decode but not yet decoded are freed.
- `flush(reset)` explicitly states that buffers submitted for decode but not yet decoded are freed, and pending output frames are returned to the output pool.
- Upstream guidance also states that when entering `FLUSHING` or `STOPPING`, the decoder shall free any AV buffers it is holding.

This means `STOPPING` and `FLUSHING` act as mandated cleanup boundaries for coded input buffers and queued output work.

## Control flow and data flow

### Control flow (session lifecycle)

A typical session from a client perspective:

1. Discover decoder IDs and modes:
   - `IVideoDecoderManager.getVideoDecoderIds()`
   - `IVideoDecoderManager.getSupportedOperationalModes()`

2. Obtain decoder instance:
   - `IVideoDecoderManager.getVideoDecoder(id)`

3. Create AV Buffer pool for coded input:
   - `IAVBuffer.createVideoPool(secureHeap, videoDecoderId, spaceListener)`

4. Register event listener (recommended):
   - `IVideoDecoder.registerEventListener(eventListener)`

5. Open session:
   - `IVideoDecoder.open(codec, secure, controllerListener)`
   - Observe `CLOSED -> OPENING -> READY` via `onStateChanged`.

6. Start:
   - `IVideoDecoderController.start()`
   - Observe `READY -> STARTING -> STARTED`.

7. Steady-state decode loop:
   - allocate and fill coded buffers from AV Buffer pool
   - call `decodeBuffer(pts, handle)` repeatedly
   - receive outputs via `onFrameOutput(...)` and `onUserDataOutput(...)`

8. Optional mid-stream controls:
   - `parseCodecSpecificData(...)` (typically early in started state before frames)
   - `signalDiscontinuity()`
   - `flush(reset)` for recovery or seek-like behavior
   - `signalEOS()` for drain

9. Stop and close:
   - `IVideoDecoderController.stop()` → `READY`
   - `IVideoDecoder.close(controller)` → `CLOSED`

10. Teardown:
   - ensure all handles freed
   - `IAVBuffer.destroyPool(pool)`

### Data flow (buffer handles and timestamps)

#### Coded input data (client → decoder)

- The coded frame payload resides in an AV Buffer allocation (`long bufferHandle`), typically allocated via `IAVBuffer.alloc(pool, size)`.
- The timestamp is passed out-of-band as `nsPresentationTime` in nanoseconds.
- Each `decodeBuffer(...)` references exactly one frame and its PTS.

Ownership:
- On `decodeBuffer(...) == true`, ownership of `bufferHandle` transfers to the decoder; the client must not modify or free it.
- The decoder must free the handle when finished consuming it.

Backpressure:
- If `decodeBuffer(...) == false`, the decoder did not accept the buffer; the client retains ownership and must decide whether to retry or free/reuse.

#### Decoded output (decoder → client) in non-tunnelled mode

- The decoder returns decoded frames via `onFrameOutput(pts, frameBufferHandle, metadata)` with `frameBufferHandle` being a valid AV Buffer handle.
- The client (or downstream) must eventually free `frameBufferHandle` via `IAVBuffer.free(...)`.

#### Decoded output (decoder internal) in tunnelled mode

- No decoded frame handles are returned to the client.
- If metadata must still be delivered, `onFrameOutput(..., -1, metadata)` may be used.
- If operating exclusively tunnelled and no metadata needs to be delivered, `onFrameOutput()` should not be called.

## Buffer lifecycle and ownership rules

### AV Buffer heap/pool/handle lifecycle

1. Pool creation:
   - Client creates a pool with `createVideoPool(secureHeap, videoDecoderId, listener)`.
   - The `listener` must remain valid for the lifetime of the pool and should not be destroyed before `destroyPool()` returns.

2. Allocation:
   - Client allocates coded buffers from the pool using `alloc(pool, size)`.
   - Allocation is immediate-or-fail.

3. Use:
   - For non-secure buffers, the client typically maps and writes frame bytes (helper-library dependent).
   - For secure buffers, the client must not expect direct mapping; secure write/copy is platform dependent.

4. Ownership transfer and freeing:
   - Coded input handles are freed by the decoder after consumption.
   - Decoded output handles (non-tunnelled) are freed by the client/downstream after presentation/consumption.

5. Pool destruction:
   - Pool destruction requires **no outstanding allocations**.
   - `destroyPool(pool)` fails with service-specific `HALError::NOT_EMPTY` if any allocations remain outstanding.

### Typical ownership transitions

The “swap” is intentional and enabled by a global handle namespace:

- Coded input:
  - Allocated by client → submitted to decoder → freed by decoder.

- Decoded output (non-tunnelled):
  - Allocated by decoder (vendor-private output pool) → delivered to client → freed by client/downstream.

### Output frame pool constraints (non-tunnelled)

Upstream guidance describes a vendor-private decoded-frame pool whose size can be reported via a property (commonly referred to as `OUTPUT_FRAME_POOL_SIZE`). When this pool is empty:
- The decoder cannot output the next decoded frame.
- The decoder may either buffer more coded inputs internally or apply backpressure by returning `false` from `decodeBuffer()`.

Client implication:
- If `decodeBuffer()` starts returning `false` under load, ensure decoded output frame handles are being freed promptly downstream.

## Typical sequences (with error/alt paths)

This section provides narrative “sequence templates” to implement client-side robustness. The ASCII sequences are simplified; see the local sequence diagram page for more.

### Sequence A: Open → start → decode (non-tunnelled)

1. Create pool (secure or non-secure):
   - `pool = createVideoPool(secureHeap, videoDecoderId, spaceListener)`

2. `controller = videoDecoder.open(codec, secure, controllerListener)`
   - Observe state transitions: `CLOSED->OPENING->READY`.

3. `controller.start()`
   - Observe: `READY->STARTING->STARTED`.

4. For each frame:
   - `handle = avBuffer.alloc(pool, size)`
   - Fill handle (mapping/copy rules depend on secure mode)
   - `accepted = controller.decodeBuffer(ptsNs, handle)`
     - If `accepted == true`: do not free or modify handle; decoder will free it.
     - If `accepted == false`: client retains ownership; retry later or free/reuse.

5. Output:
   - In non-tunnelled mode: expect `onFrameOutput(ptsNs, frameHandle, metadataOrNull)`
   - Free `frameHandle` when downstream done: `avBuffer.free(frameHandle)`.

### Sequence B: Decoder backpressure (`decodeBuffer` returns false)

Situation:
- `decodeBuffer(...)` returns `false` meaning decode buffer is full.

Typical causes:
- Internal coded-input queue depth limit.
- Output pool empty (non-tunnelled) because downstream holds decoded frames too long.
- Tunnelled downstream congestion in vendor path.

Client response:
- Do not assume handle ownership was transferred.
- Apply retry with pacing; avoid tight spin loops.
- Ensure downstream is freeing decoded output handles quickly enough (non-tunnelled).
- Consider `flush(reset=false)` if the pipeline is stuck and you need to force release of queued inputs inside the decoder.

### Sequence C: AV Buffer out-of-memory during `alloc()`

Situation:
- `IAVBuffer.alloc(pool, size)` fails with service-specific `HALError::OUT_OF_MEMORY`.

Client response:
1. Call `notifyWhenSpaceAvailable(pool, size)`.
2. Wait for `IAVBufferSpaceListener.onSpaceAvailable()`.
3. Retry allocation.

Important lifetime constraint:
- Keep the `IAVBufferSpaceListener` alive for the pool lifetime. Do not drop it until after `destroyPool()` succeeds.

### Sequence D: Flush (with reset vs without)

Situation:
- Client calls `flush(reset)` in `STARTED`.

Effects:
- Decoder frees any submitted input buffers not yet decoded.
- Decoder returns any pending output frames due for callback back to the output pool.
- If `reset=true`, the internal decode state is fully reset back to its opened baseline.

Client response:
- After flush, expect metadata to be delivered non-null on the first output frame (per metadata rules).
- Resume `decodeBuffer` submissions after flush completes and state is back to `STARTED`.

### Sequence E: Stop and close (and why ordering matters)

Ordering:
1. `controller.stop()` (if in STARTED) and wait for transition back to `READY`.
2. `videoDecoder.close(controller)` and wait for transition to `CLOSED`.
3. Free any outstanding client-owned handles (including decoded output handles in non-tunnelled).
4. `avBuffer.destroyPool(pool)`.

Why:
- If the decoder still holds accepted coded input handles, destroying the pool may fail (and in general teardown will be incomplete).
- `destroyPool()` explicitly requires the pool to be empty.

### Sequence F: `destroyPool()` fails with NOT_EMPTY

Situation:
- `destroyPool(pool)` fails with service-specific `HALError::NOT_EMPTY`.

Likely causes:
- The client still holds coded input handles that were never accepted (for example, because `decodeBuffer` returned `false`).
- In non-tunnelled mode, the client/downstream still holds decoded frame handles.
- The decoder session wasn’t stopped/closed cleanly, and the decoder still holds coded handles.

Mitigation:
- Track and free all handles you own.
- Ensure stop/close have completed.
- Ensure all decoded output handles are freed.
- Retry `destroyPool()`.

### Sequence G: Invalid handle usage

AV Buffer APIs provide several “invalid handle” surfaces:
- `alloc(...)` can return `INVALID_HANDLE` (-1) for invalid pool handle or size > pool size.
- `free(handle)` returns `false` when handle is invalid.
- `isValid(handle)` can be used defensively or for debug.

Decoder-side invalid handle implications:
- If the client submits an invalid handle to `decodeBuffer(...)`, it may receive `EX_ILLEGAL_ARGUMENT` or a decode error callback depending on implementation, but correct client behavior is to validate handle management and not submit freed/unallocated handles.

## Secure vs non-secure modes

### Secure vs non-secure session selection

The secure pipeline is selected at open time:
- `IVideoDecoder.open(codec, secure, ...)`

The memory heap type must match:
- secure session should use `IAVBuffer.createVideoPool(true, ...)`.
- non-secure session should use `IAVBuffer.createVideoPool(false, ...)`.

Mixing secure and non-secure handles within a single session is not valid pipeline behavior and risks either rejected buffers or incorrect security boundaries.

### Secure heap constraints

Secure AV buffers:
- Should not be mappable into untrusted client processes.
- Require vendor/platform mechanisms for secure write/copy into secure buffers.
- Must remain within secure video path requirements (no exposure of decoded secure content outside allowed boundaries).

### Tunnelled vs non-tunnelled impact on security

Tunnelled mode is often preferred for secure playback because decoded frames are not exposed as client-accessible handles. Non-tunnelled mode can still support secure playback if secure decoded frames are represented by secure handles that clients/downstream can pass and free without mapping.

## Configuration inputs and capability discovery

### Codec selection

Codec is selected via:
- `IVideoDecoder.open(in Codec codec, in boolean secure, ...)`

The `Codec` enum includes (at least):
- `MPEG2_VIDEO`, `H264_AVC`, `H265_HEVC`, `VP9`, `AV1`.

If the codec is not supported, `open(...)` returns `null`.

### Operational mode selection

Platform supported modes are discovered via:
- `IVideoDecoderManager.getSupportedOperationalModes()`

The `OperationalMode` enum includes:
- `TUNNELLED`
- `NON_TUNNELLED`
- `GRAPHICS_TEXTURE` (optional)

The operational mode that a session uses is commonly controlled via a property (as described upstream). The exact property keys are defined in the video decoder Property enum (not reproduced here), but the design intent is:
- Mode is a configuration input that can change buffer ownership patterns (decoded frame handles vs tunnelled output).

### Capabilities discovery

Resource capabilities are returned by:
- `IVideoDecoder.getCapabilities()`

The capabilities include:
- Supported codecs (`supportedCodecs`)
- Supported dynamic ranges (`supportedDynamicRanges`)
- Secure support flag (`supportsSecure`)

Design implication:
- Clients should check `supportsSecure` before attempting `open(..., secure=true, ...)`.
- Clients may use codec capabilities to pre-select format and avoid open failures.

### Codec specific data (CSD)

Some streams require CSD to be provided out-of-band:
- `IVideoDecoderController.parseCodecSpecificData(csdVideoFormat, codecData)`

Rules:
- Must be called in `STARTED`.
- Must be called before feeding frame buffers when required.
- The format depends on `csdVideoFormat` and must match the codec selected in `open()`.

This call is a common integration point for demux/container parsing layers (which extract CSD from container headers) and the decoder session (which needs it for correct initialization).

## Performance and throughput considerations

### Throughput constraints and backpressure surfaces

The design intentionally provides two distinct flow control mechanisms:

1. Decoder-side backpressure:
   - `decodeBuffer(...)` returns `false` when internal decode buffer is full.
   - This is a normal condition and should be handled with pacing.

2. Memory/pool-side backpressure:
   - `IAVBuffer.alloc(...)` can fail due to `OUT_OF_MEMORY`.
   - Intended recovery is `notifyWhenSpaceAvailable(...)` + `onSpaceAvailable()`.

Clients should treat these as complementary:
- The pipeline can be unable to allocate new coded frames even if the decoder could accept them.
- The pipeline can be able to allocate coded frames but unable to submit them because decoder queues are full.

### Metadata emission optimization

The contract that metadata can be null when unchanged is specifically intended to reduce CPU load on high-frame-rate streams. Implementations and clients should both follow the rule:
- Ensure first frame after start/flush carries metadata.
- Ensure metadata is sent when changes occur.
- Otherwise omit metadata to avoid per-frame overhead.

### Buffer pool sizing and handle churn

Pool sizes (and output frame pool sizes) directly affect:
- Burst tolerance (how many frames can be buffered).
- Latency and jitter (especially with B-frame reorder and non-tunnelled output).
- Frequency of backpressure conditions.

When integrating in a real pipeline, consider:
- Target bitrates and maximum coded frame sizes.
- Worst-case jitter and decode reorder windows.
- Downstream presentation buffering depth.

## Key error paths and how to handle them

### Open failures

Failure modes:
- `open(...)` returns `null` if codec or secure mode is unsupported.
- `open(...)` throws `EX_ILLEGAL_STATE` if not in `CLOSED`.
- `open(...)` throws `EX_ILLEGAL_ARGUMENT` for invalid parameters.
- `open(...)` throws `EX_NULL_POINTER` for null listener objects.

Client handling:
- Validate codec/secure support via capabilities first.
- Enforce correct state ordering (close previous session cleanly).
- Treat exceptions as programming/integration errors to fix, not runtime conditions to ignore.

### Start/stop/flush failures

Failure modes:
- `start()` / `stop()` / `flush()` throw `EX_ILLEGAL_STATE` if called in wrong state.

Client handling:
- Use `getState()` and/or track `onStateChanged` transitions to gate calls.
- Avoid calling `close()` while started; stop first, wait for READY, then close.

### decodeBuffer invalid arguments and backpressure

Failure modes:
- `decodeBuffer` throws `EX_ILLEGAL_STATE` if not `STARTED`.
- `decodeBuffer` throws `EX_ILLEGAL_ARGUMENT` for invalid inputs.
- `decodeBuffer` returns `false` under backpressure (decode buffer full).

Client handling:
- Treat `false` as flow control, not fatal error.
- Do not transfer ownership on `false`.
- Maintain handle tracking to avoid leaks (especially with retries).

### destroyPool NOT_EMPTY

Failure mode:
- Service-specific `HALError::NOT_EMPTY` when allocations remain.

Client handling:
- Ensure all handles have been freed.
- Audit ownership: you must free all handles you still own; the decoder frees coded input handles it accepted; you/free downstream frees decoded output handles in non-tunnelled mode.
- Re-run teardown steps: stop/close session before destroying pool.

### Invalid handle

Failure modes:
- `IAVBuffer.free(handle)` returns `false`.
- `IAVBuffer.isValid(handle)` returns `false`.

Client handling:
- Do not double-free.
- Do not submit freed handles to `decodeBuffer`.
- Consider debug tracking: per-pool handle registry and ownership state (client-owned vs decoder-owned vs downstream-owned).

## Cross-links to local pipeline documentation

The repository already contains detailed local pipeline notes; this design doc is intended to complement them.

For deeper dives, see:
- [Pipeline components](../videodecoder/pipeline_components.md)
- [Pipeline control flow](../videodecoder/pipeline_control_flow.md)
- [Pipeline dataflow](../videodecoder/pipeline_dataflow.md)
- [Pipeline buffer lifecycle](../videodecoder/pipeline_buffer_lifecycle.md)
- [ASCII sequence diagrams](../videodecoder/sequence_diagram.md)

## References

### Upstream HALIF references (provided)

- Video Decoder HALIF documentation (current):
  - https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

- AV Buffer HALIF documentation (current):
  - https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

### Local workspace sources used to ground this document

Video Decoder AIDL:
- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderManager.aidl`
- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`
- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`
- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderEventListener.aidl`
- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderControllerListener.aidl`
- `rdk-halif-aidl-17/common/current/com/rdk/hal/State.aidl`

AV Buffer AIDL:
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/Pool.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBufferSpaceListener.aidl`

Local videodecoder pipeline docs (cross-linked above):
- `kavia-docs/videodecoder/pipeline_components.md`
- `kavia-docs/videodecoder/pipeline_control_flow.md`
- `kavia-docs/videodecoder/pipeline_dataflow.md`
- `kavia-docs/videodecoder/pipeline_buffer_lifecycle.md`
- `kavia-docs/videodecoder/sequence_diagram.md`
