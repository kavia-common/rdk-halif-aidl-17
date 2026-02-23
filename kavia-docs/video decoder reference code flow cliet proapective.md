<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder/video decoder reference code flow cliet proapective.md
- Operation: write
- Timestamp: 2026-02-10T13:12:44.153647+00:00
- Restored At: 2026-02-23T05:04:11.251700+00:00
- Task ID: cm219d4578
-->

# Video Decoder Reference Code Flow (Client Perspective)

## Overview

This document describes a **client-perspective reference code flow** for using the RDK HALIF **Video Decoder** AIDL interfaces together with the **AV Buffer** service for coded-input buffer allocation and lifecycle management.

It is written as a practical “what a client does” narrative that follows the normative contracts in the AIDL comments and the upstream HALIF documentation. It also cross-links the existing local videodecoder pipeline documents in this repository so you can jump to deeper control-flow/dataflow/lifecycle discussions.

Local related docs (cross-links):

- Video Decoder pipeline control flow: `kavia-docs/videodecoder/pipeline_control_flow.md`
- Video Decoder pipeline dataflow: `kavia-docs/videodecoder/pipeline_dataflow.md`
- Video Decoder buffer lifecycle: `kavia-docs/videodecoder/pipeline_buffer_lifecycle.md`
- Video Decoder sequence diagrams: `kavia-docs/videodecoder/sequence_diagram.md`
- AV Buffer reference code flow (client perspective): `kavia-docs/avbuffer-reference-code-flow-client-perspective.md`

Upstream references (deep links are repeated in the final References section):

- Video Decoder HALIF docs: https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/
- AV Buffer HALIF docs: https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

## Prerequisites/Capabilities discovery

A robust client typically starts by discovering **what the platform supports**, then chooses an operational strategy (tunnelled vs non-tunnelled, secure vs non-secure, codec).

### 1) Discover decoder resources and supported operational modes

The Video Decoder HAL is resource-based. You discover available decoder IDs from the manager:

- `IVideoDecoderManager.getVideoDecoderIds() -> IVideoDecoder.Id[]`
- `IVideoDecoderManager.getSupportedOperationalModes() -> OperationalMode[]`
- `IVideoDecoderManager.getVideoDecoder(id) -> @nullable IVideoDecoder`

Client implications:

The returned operational modes are defined to be stable across calls, and each decoder resource must share the same operational mode(s) as other decoder resources on the platform.

If `getVideoDecoder(id)` returns `null`, the ID is invalid.

Relevant local sources:

- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderManager.aidl`

### 2) Query per-resource capabilities

Once you have an `IVideoDecoder` resource instance, query capabilities:

- `IVideoDecoder.getCapabilities() -> Capabilities`

The `Capabilities` parcelable includes:

- `supportedCodecs` (array of `CodecCapabilities`)
- `supportedDynamicRanges` (array of `DynamicRange`)
- `supportsSecure` (boolean)

Client implications:

If `supportsSecure` is `false`, the client should not expect `open(codec, secure=true, ...)` to succeed for that resource.

Relevant local sources:

- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`
- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/Capabilities.aidl`

### 3) Decide secure vs non-secure early

Secure mode is selected at **open time**:

- `IVideoDecoder.open(codec, secure, controllerListener)`

If the client intends secure decode, it should also plan to allocate coded input buffers from a **secure AV Buffer heap** (secure pool) and avoid any non-secure mapping behavior.

## Session setup (listeners, open)

Video Decoder uses two distinct callback channels:

1. `IVideoDecoderEventListener` (resource-level): state changes and decode errors.
2. `IVideoDecoderControllerListener` (session/controller-level): decoded output callbacks and user data.

### 1) Register a resource event listener (recommended)

Before opening, many clients register an event listener:

- `IVideoDecoder.registerEventListener(videoDecoderEventListener) -> boolean`

Callbacks:

- `onStateChanged(State oldState, State newState)`
- `onDecodeError(ErrorCode errorCode, int vendorErrorCode)`

Client implications:

Although you can poll `IVideoDecoder.getState()`, state transitions are asynchronous and are authoritatively communicated via `onStateChanged`.

Relevant local sources:

- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderEventListener.aidl`
- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`

### 2) Create a controller listener (required for open)

Open requires a non-null `IVideoDecoderControllerListener`:

- `IVideoDecoderControllerListener.onFrameOutput(ptsNs, frameBufferHandle, @nullable FrameMetadata metadata)`
- `IVideoDecoderControllerListener.onUserDataOutput(ptsNs, byte[] userData)`

Client implications:

`onFrameOutput` is where decoded output is delivered in non-tunnelled mode, and where metadata/EOS is delivered (including in tunnelled mode using `frameBufferHandle = -1`).

The `metadata` parameter is allowed to be `null` if unchanged, but must be non-null on the first frame after `start()` or `flush()`, and when metadata changes.

Relevant local source:

- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderControllerListener.aidl`

### 3) Open the decoder (resource must be CLOSED)

Open creates the session controller:

- `IVideoDecoder.open(codec, secure, controllerListener) -> @nullable IVideoDecoderController`

Preconditions and outcomes:

The resource must be in `State::CLOSED`, otherwise the call can fail with `EX_ILLEGAL_STATE`.

On success, the decoder transitions `CLOSED -> OPENING -> READY` (observable via `onStateChanged`) and returns a controller used for start/stop/flush/decode.

`open` may return `null` if the codec or secure-mode is not supported.

Crash-safety behavior:

The contract states that if the client that opened the controller crashes, the service will implicitly call `stop()` and `close()` to clean up.

Relevant local source:

- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`

## Start/Stop/Close

The canonical lifecycle is:

1. `open(...)` (CLOSED -> READY)
2. `start()` (READY -> STARTED)
3. steady-state decode operations
4. `stop()` (STARTED -> READY)
5. `close(controller)` (READY -> CLOSED)

This aligns with the more detailed narrative and state-transition discussion in `kavia-docs/videodecoder/pipeline_control_flow.md`.

### Start

- `IVideoDecoderController.start()`

Precondition:

The resource must be in `State::READY`.

State transitions:

The decoder transitions `READY -> STARTING -> STARTED` and notifies the event listener.

Relevant local source:

- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`

### Stop

- `IVideoDecoderController.stop()`

Precondition:

The resource must be in `State::STARTED`.

Buffer cleanup semantics (important for clients):

Stop transitions through `STOPPING` and automatically frees any **input buffers already submitted** but not yet decoded, and then returns to `READY`. This is effectively a flush and reset.

Relevant local source:

- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`

### Close

- `IVideoDecoder.close(controller) -> boolean`

Precondition:

The resource must be in `State::READY`.

State transitions:

The decoder transitions `READY -> CLOSING -> CLOSED` and notifies the event listener.

Client implication:

If you are `STARTED`, stop first and wait for `READY` before closing.

Relevant local source:

- `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`

## Buffer acquisition via AV Buffer (pool creation, alloc/free, ownership)

A Video Decoder client typically uses AV Buffer to allocate **coded input** buffers. The same AV Buffer service is also used to free decoded output handles (in non-tunnelled mode), because handles are globally free-able.

For deeper AV Buffer reference behavior (including notifyWhenSpaceAvailable and helper mapping/OOB IPC), see:

- `kavia-docs/avbuffer-reference-code-flow-client-perspective.md`

### 1) Acquire the AV Buffer service

Client obtains `IAVBuffer` (service name constant: `"AVBuffer"`).

Relevant local source:

- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`

### 2) Create a video pool targeted to the decoder resource

Create a pool, choosing secure or non-secure heap depending on your session:

- `IAVBuffer.createVideoPool(secureHeap, videoDecoderId, IAVBufferSpaceListener listener) -> Pool`

Client implications:

The `videoDecoderId` must be one previously obtained from `IVideoDecoderManager.getVideoDecoderIds()`, otherwise `EX_ILLEGAL_ARGUMENT` can occur.

Pool creation can fail with out-of-memory (`EX_SERVICE_SPECIFIC` with `HALError::OUT_OF_MEMORY`), or return a pool with `Pool.handle = Pool.INVALID_POOL` depending on implementation and how you interpret “on failure handle invalid” in the AIDL comment.

Listener lifetime rule:

The listener must remain valid for the entire lifetime of the pool, because it is the callback target for `notifyWhenSpaceAvailable`.

Relevant local sources:

- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBufferSpaceListener.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/Pool.aidl`

### 3) Allocate coded input buffers

Allocate coded input buffers:

- `IAVBuffer.alloc(poolHandle, size) -> long bufferHandle`

Client implications:

On pool exhaustion, `alloc` can throw `EX_SERVICE_SPECIFIC` with `HALError::OUT_OF_MEMORY`. The expected recovery is to call `notifyWhenSpaceAvailable(pool, size)` and wait for `onSpaceAvailable()`.

If `alloc` returns `IAVBuffer.INVALID_HANDLE` (-1), that indicates invalid pool handle or invalid size in the “invalid argument” sense described by the AIDL.

### 4) Free coded or decoded buffers

Free is a single global operation:

- `IAVBuffer.free(bufferHandle) -> boolean`

Ownership implications are covered in later sections, but the critical point is that the AIDL contract allows freeing by “other components,” enabling these two key transfers:

The client allocates coded buffers and the decoder frees them after consumption.

The decoder allocates decoded frame buffers (non-tunnelled mode) and the client/downstream frees them when finished.

## Submit coded buffers

### Steady-state submit loop

Once `start()` has completed and the session is `STARTED`, the client feeds encoded frames one at a time:

- `IVideoDecoderController.decodeBuffer(ptsNs, bufferHandle) -> boolean`

Where:

`ptsNs` is the frame presentation timestamp in nanoseconds.

`bufferHandle` is an AV Buffer handle for a coded frame.

### Ownership transfer on success

The AIDL contract is explicit:

Once the decoder has finished processing the buffer, it is automatically released and returned to the AV Buffer Manager. The caller must not modify or free the buffer after submission.

Client rule of thumb:

If `decodeBuffer(...)` returns `true`, treat ownership as transferred immediately and never touch the handle again.

If `decodeBuffer(...)` returns `false` (“decode buffer is full”), ownership did not transfer; the client must decide to retry later, reuse the buffer, or free it.

### Codec specific data (CSD) provisioning

Some streams require codec-specific configuration data to be provided out-of-band, before frame submission:

- `IVideoDecoderController.parseCodecSpecificData(csdVideoFormat, byte[] codecData) -> boolean`

Precondition:

The decoder must be in `STARTED` state.

Client implication:

When required, call this before the first `decodeBuffer(...)` for the stream.

## Output handling

Output is delivered asynchronously through the `IVideoDecoderControllerListener` you provided to `open(...)`.

### Frame output callback

- `onFrameOutput(ptsNs, frameBufferHandle, @nullable FrameMetadata metadata)`

Non-tunnelled mode:

`frameBufferHandle` is expected to be a valid decoded 2D frame buffer handle, which the client can pass downstream (for example, to a Video Sink), and must eventually be freed via `IAVBuffer.free(frameBufferHandle)` when no longer needed.

Tunnelled mode:

The decoded frames are not returned to the client. If metadata must still be delivered, `frameBufferHandle` is `-1`. The upstream/local docs explain that if operating exclusively tunnelled and metadata does not need to be passed, `onFrameOutput` should not be called.

Metadata emission rule:

Metadata must be non-null for the first frame after `start()` or `flush()`, or when metadata changes. Otherwise it may be null.

### User data callback

- `onUserDataOutput(ptsNs, byte[] userData)`

The AIDL specifies:

The user data must be delivered in the same frame presentation order as output frames, but the relative ordering between `onFrameOutput` and `onUserDataOutput` is not fixed; either can arrive first.

### EOS and discontinuities

The client can signal stream boundaries:

- `signalDiscontinuity()`: indicates PTS discontinuity affecting buffers submitted after the call.
- `signalEOS()`: indicates no more coded buffers will be submitted for this drain cycle.

EOS requirement:

After all frames have been output, an `onFrameOutput` callback must occur with `FrameMetadata.endOfStream = true`.

## Error handling and common failure paths

Video Decoder failures appear in two main ways:

1. Synchronous Binder exceptions / return values from API calls.
2. Asynchronous `onDecodeError(...)` callback.

### 1) Common synchronous failure patterns

Illegal state:

Calling `start()`, `stop()`, `flush()`, `decodeBuffer()`, `signalEOS()`, `signalDiscontinuity()`, or `parseCodecSpecificData()` in the wrong state can throw `EX_ILLEGAL_STATE` as documented in the AIDL comments.

Invalid arguments:

Invalid codec data, invalid buffer handles, invalid IDs, and other parameter issues can throw `EX_ILLEGAL_ARGUMENT` or cause a `false` return (depending on method contract).

Notably, `decodeBuffer(...)` uses both:

It can throw exceptions for illegal state/argument, but also returns `false` for decoder backpressure (“decode buffer is full”).

### 2) Asynchronous decode errors

The event listener callback:

- `IVideoDecoderEventListener.onDecodeError(ErrorCode errorCode, int vendorErrorCode)`

Client implication:

Treat `vendorErrorCode` as implementation-specific diagnostics and `ErrorCode` as the stable contract. In this workspace, `ErrorCode.aidl` currently appears to be a placeholder enum, so implementations and clients should rely on upstream docs and vendor conventions for concrete values until the enum is finalized.

### 3) AV Buffer out-of-memory and recovery

If `IAVBuffer.alloc(...)` fails with out-of-memory, the specified recovery path is:

1. Pause allocation/feeding.
2. Call `IAVBuffer.notifyWhenSpaceAvailable(pool, size)` for the allocation size you need.
3. Wait for `IAVBufferSpaceListener.onSpaceAvailable()`.
4. Retry allocation.

This is distinct from decoder backpressure. You can have both:

AV Buffer pool exhaustion prevents allocating more coded buffers.

Decoder backpressure (`decodeBuffer` returns false) prevents submitting buffers even if they are allocatable.

### 4) Teardown failure: destroyPool NOT_EMPTY

`IAVBuffer.destroyPool(pool)` requires all allocations from that pool to be freed first; otherwise it can throw `HALError::NOT_EMPTY`.

Common root causes:

The client retained coded input buffers that were never successfully transferred (because `decodeBuffer` returned false and the client never freed/reused them).

The client/downstream is still holding decoded output frame handles (non-tunnelled mode) and has not freed them.

The decoder session is still alive or not stopped/closed and still holds accepted coded inputs.

Recommended ordering:

1. `stop()` (if started) and wait for `READY`.
2. `close(controller)`.
3. Free any client-held coded input handles not transferred.
4. Ensure all decoded output handles are freed (non-tunnelled).
5. `destroyPool(pool)`.

## Secure vs non-secure notes

Secure decode is a pipeline-wide choice that affects both open-time configuration and buffer allocation.

### Selecting secure decode

Open with secure enabled:

- `IVideoDecoder.open(codec, secure=true, controllerListener)`

The resource must advertise `Capabilities.supportsSecure = true` for secure to be expected to work.

### Using secure AV Buffer heaps

Allocate coded input from a secure pool:

- `IAVBuffer.createVideoPool(secureHeap=true, videoDecoderId, listener)`

Client implications:

Secure buffers generally cannot be mapped into untrusted processes. Filling secure-coded buffers may require platform/vendor-specific mechanisms.

Avoid mixing secure and non-secure buffer handles within one session. Keep the “secure-ness” consistent across the decoder open flag and the AV Buffer heap choice.

### Output implications

Non-tunnelled secure decode can still deliver decoded frame handles to the client, but those handles may represent secure memory that downstream must treat as opaque and must not map in non-secure contexts.

Tunnelled secure decode can reduce exposure because decoded frames never leave the vendor pipeline as handles.

## References (deep links)

### Upstream HALIF documentation

- Video Decoder HALIF docs (current): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/
- AV Buffer HALIF docs (current): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

### Local repository AIDL sources used to ground this flow

- Video Decoder manager: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderManager.aidl`
- Video Decoder resource: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`
- Video Decoder controller: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`
- Video Decoder controller callbacks: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderControllerListener.aidl`
- Video Decoder event callbacks: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderEventListener.aidl`
- Video Decoder capabilities: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/Capabilities.aidl`
- Video Decoder error codes: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/ErrorCode.aidl`

- AV Buffer interface: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- AV Buffer pool type: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/Pool.aidl`
- AV Buffer space listener: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBufferSpaceListener.aidl`

### Existing local videodecoder pipeline docs (cross-links)

- `kavia-docs/videodecoder/pipeline_control_flow.md`
- `kavia-docs/videodecoder/pipeline_dataflow.md`
- `kavia-docs/videodecoder/pipeline_buffer_lifecycle.md`
- `kavia-docs/videodecoder/sequence_diagram.md`
