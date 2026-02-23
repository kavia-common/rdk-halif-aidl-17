<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder/pipeline_control_flow.md
- Operation: write
- Timestamp: 2026-02-10T12:45:50.138627+00:00
- Restored At: 2026-02-23T05:04:11.251356+00:00
- Task ID: cm219d4578
-->

# Video Decoder Pipeline Control Flow (HALIF AIDL)

## Overview

This document describes the Video Decoder pipeline control flow from a client’s perspective, focusing on the sequence of AIDL calls, the state transitions observable via callbacks, and the points where AV Buffer handles are allocated, transferred, and freed.

The control flow is defined by the interaction between:

1. The Video Decoder HAL resource/session interfaces (`IVideoDecoder` and `IVideoDecoderController`) and their oneway callbacks (`IVideoDecoderEventListener`, `IVideoDecoderControllerListener`).
2. The AV Buffer HAL (`IAVBuffer`) used to allocate coded input frame buffers (and to free both coded and decoded-frame buffers via a single global handle namespace).

This document is implementation-aware in the sense that it follows the normative contracts expressed in the AIDL comments and the upstream HALIF documentation, but it does not assume a particular vendor implementation beyond those contracts.

Upstream references:
- Video Decoder HALIF docs: https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/
- AV Buffer HALIF docs: https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

## Control Flow Stages (initialize/open/start/decode/flush/stop/close)

### Initialize (service discovery and resource selection)

A typical client begins by selecting a decoder resource ID and preparing an AV Buffer pool that will be used to allocate coded input frame buffers.

In HALIF terms, “initialize” includes:
1. Acquiring `IVideoDecoderManager` (service lookup).
2. Enumerating decoder resources via `getVideoDecoderIds()` and selecting an `IVideoDecoder.Id`.
3. Obtaining an `IVideoDecoder` instance for that ID (via the manager).
4. Creating a video pool via `IAVBuffer.createVideoPool(secureHeap, videoDecoderId, listener)`.

The pool step is often done early because pool creation can fail due to platform memory constraints and because the pool’s `IAVBufferSpaceListener` must remain alive for the lifetime of the pool.

Upstream reference points:
- Video Decoder overview (resource model): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/
- AV Buffer pool creation and listener lifetime: https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

### Open (create a decoder session/controller)

“Open” corresponds to calling `IVideoDecoder.open(codec, secure, controllerListener)` on a resource that is currently `CLOSED`.

If the call is successful:
- The resource transitions `CLOSED -> OPENING -> READY`.
- The returned `IVideoDecoderController` becomes the handle for session lifecycle control and coded-frame submission.
- The provided `IVideoDecoderControllerListener` is the destination for decoded output callbacks such as `onFrameOutput(...)` and `onUserDataOutput(...)`.

The AIDL also specifies crash-safety behavior: if the client that opened the controller crashes, the service implicitly calls `stop()` and `close()` to perform cleanup. Clients should therefore treat open/start/decode as operations whose resources will be reclaimed if the client process dies.

Implementation note:
- `open(...)` can return `null` if the codec or secure-mode is not supported. Separately, it can throw `EX_ILLEGAL_STATE` if the resource is not `CLOSED`.

Deep links:
- Video Decoder HALIF “open/close” semantics and state management: https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/
- AIDL contract for `open(...)`: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`

### Start (transition to STARTED)

Once a controller exists and the resource is `READY`, the client calls `IVideoDecoderController.start()`.

A compliant server transitions:
- `READY -> STARTING -> STARTED`

The `STARTED` state is the precondition for all data-plane operations such as `decodeBuffer(...)`, `flush(...)`, `signalDiscontinuity()`, `signalEOS()`, and `parseCodecSpecificData(...)`.

Deep links:
- Session state management in Video Decoder HALIF docs: https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/
- AIDL contract for `start()`: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`

### Decode (feed coded buffers, receive output callbacks)

Decode is a tight loop of:
1. Allocating/filling coded input buffers via `IAVBuffer.alloc(pool, size)` (client-side).
2. Submitting coded frame handles to the decoder via `IVideoDecoderController.decodeBuffer(ptsNs, bufferHandle)` (client-side).
3. Receiving output notifications via `IVideoDecoderControllerListener` callbacks (server-to-client).
4. Freeing coded input handles after consumption (server-side behavior, per contract).
5. Freeing decoded output handles when downstream is finished (client-side, non-tunnelled mode).

#### Coded input submission and ownership transfer

`decodeBuffer(...)` submits one encoded video frame, identified by `(ptsNs, bufferHandle)`. The AIDL states that once the decoder finishes processing the buffer, it is automatically released and returned to the AV Buffer Manager, and the caller must not modify or free the buffer after submission.

In practical pipeline terms, once `decodeBuffer(...)` returns `true`, the caller should treat the coded input handle as “owned by the decoder.” If the call returns `false`, ownership is not transferred; the client must decide whether to retry later, reuse the buffer, or free it.

Decoder-side backpressure is explicitly expressed as `decodeBuffer(...)` returning `false` “if the decode buffer is full.” This is distinct from AV Buffer pool out-of-memory.

Deep links:
- Video Decoder buffer submission behavior: https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/
- AIDL contract for `decodeBuffer(...)`: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`

#### Output callbacks: frame output and user data

Decoded output is delivered asynchronously via the oneway controller listener:

- `onFrameOutput(nsPresentationTime, frameBufferHandle, metadata)`
- `onUserDataOutput(nsPresentationTime, userData)`

Metadata emission is conditional to reduce CPU load. The AIDL states that `metadata` must be non-null on the first frame after `start()` or `flush()` or when metadata changes, and can be null when unchanged.

Tunnelled vs non-tunnelled behavior affects `frameBufferHandle`:
- In non-tunnelled mode, `frameBufferHandle` is the handle to a decoded 2D frame buffer that the client must eventually free via `IAVBuffer.free(handle)`.
- In tunnelled mode, no decoded frame handle is delivered; when metadata must still be delivered, `frameBufferHandle = -1` is used.

Deep links:
- Frame output and metadata rules (Video Decoder HALIF docs): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/
- AIDL contract for `onFrameOutput(...)`: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderControllerListener.aidl`

#### EOS and discontinuity signaling (control signals during decode)

Two control signals are part of the “decode” stage in practice:

- `signalDiscontinuity()` indicates PTS discontinuity boundaries for buffers submitted after the call.
- `signalEOS()` indicates end-of-stream after the last buffer has been submitted, and requires that after all frames are output, a final `onFrameOutput(...)` occurs where `FrameMetadata.endOfStream` is `true`.

After `signalEOS()`, no more AV buffers are expected unless the session is flushed or stopped and started again.

Deep links:
- AIDL contracts: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`
- Video Decoder HALIF docs (EOS/discontinuity narrative): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

### Flush (free queued inputs, optionally reset internal decode state)

Flush is initiated via `IVideoDecoderController.flush(reset)` and is only valid while `STARTED`.

The AIDL contract specifies:
- Any input data buffers that have been passed for decode but have not yet been decoded are automatically freed.
- Any pending decoded video frames due for callback are returned to the video frame buffer pool.
- The internal Video Decoder state is optionally reset; when `reset=true`, it is fully reset “back to its opened `READY` state” baseline (while remaining in a started session from the client control flow perspective).

Flush is the primary “recovery boundary” used in running pipelines to handle errors, discontinuities that require internal purge, and “drain and restart” patterns without closing the resource.

Implementation note:
- Even though the local docs commonly describe a `FLUSHING` transitory state, the contract that matters for control-flow robustness is the buffer-release guarantee: queued coded input buffers must be freed during flush.

Deep links:
- AIDL contract for `flush(reset)`: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`
- Video Decoder HALIF docs (session state transitions): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

### Stop (quiesce decoding, free queued inputs, return to READY)

Stop is initiated via `IVideoDecoderController.stop()` and is only valid while `STARTED`.

The AIDL contract describes stop as a flush-like operation:
- The decoder enters `STOPPING`.
- Any input buffers passed for decode but not yet decoded are automatically freed.
- Once buffers are freed and internal state is reset, the decoder enters `READY`.

Operationally, stop is used for “pause-by-stop” strategies, pipeline teardown preparation, and reconfiguration that requires READY-only operations (for properties that must not be changed while started).

Deep links:
- AIDL contract for `stop()`: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`
- Video Decoder HALIF docs (state model narrative): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

### Close (release the session and return the resource to CLOSED)

Close is invoked on the resource via `IVideoDecoder.close(controller)` and requires the resource to be in `READY`.

If successful:
- The resource transitions `READY -> CLOSING -> CLOSED`
- The controller is no longer valid for control or decode calls.

In a well-behaved pipeline, close is preceded by stop (if started) and followed by pool teardown once no handles are outstanding.

Deep links:
- AIDL contract for `close(controller)`: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`
- Video Decoder HALIF docs (open/close flow): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

## Eventing and State Transitions

The Video Decoder uses asynchronous oneway eventing for two distinct concerns:

1. Resource/session state transitions via `IVideoDecoderEventListener.onStateChanged(oldState, newState)`.
2. Decode-time errors via `IVideoDecoderEventListener.onDecodeError(errorCode, vendorErrorCode)`.

The state enum used in callbacks is the common HAL state enum (`com.rdk.hal.State`) with the following values: `UNKNOWN`, `CLOSED`, `OPENING`, `READY`, `STARTING`, `STARTED`, `FLUSHING`, `STOPPING`, `CLOSING`.

A practical control-flow implication is that clients should be tolerant of transitory states and treat state notifications as authoritative; the typical request/transition is initiated by a synchronous call (e.g., `start()`), but the completion is observed asynchronously.

A simplified state progression (typical success path) looks like:

```
CLOSED --open()--> OPENING --> READY --start()--> STARTING --> STARTED
STARTED --flush()--> FLUSHING --> STARTED
STARTED --stop()--> STOPPING --> READY --close()--> CLOSING --> CLOSED
```

Error-related eventing guidance:
- Programming mistakes (wrong state, invalid arguments, null pointers) are commonly signaled via Binder exceptions on the calling thread.
- Runtime decode failures are signaled asynchronously via `onDecodeError(...)` and may require the client to stop/flush/close depending on severity.

Deep links:
- AIDL event listener: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderEventListener.aidl`
- Common state enum: `rdk-halif-aidl-17/common/current/com/rdk/hal/State.aidl`
- Video Decoder HALIF docs (state model): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

## AV Buffer interactions in control flow

AV Buffer is not an optional “helper”; it is the handle and lifetime system used to interoperate across HALs.

### Pool creation and listener lifetime

Clients create pools using:

- `IAVBuffer.createVideoPool(secureHeap, videoDecoderId, listener)`

The `listener` is an `IAVBufferSpaceListener` used for out-of-memory recovery. Upstream AV Buffer guidance requires the listener object to remain valid for the entire lifetime of the pool and not be destroyed until after `destroyPool()` returns.

Deep links:
- AV Buffer HALIF docs (out-of-memory and notify rules): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/
- AIDL: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`

### Allocation, out-of-memory, and space-available notifications

Allocation is performed by:

- `IAVBuffer.alloc(poolHandle, size)`

If the allocation fails due to exhaustion, `alloc()` signals `HALError::OUT_OF_MEMORY` as a service-specific exception. The intended recovery is:

1. Client pauses allocations and feeding.
2. Client calls `IAVBuffer.notifyWhenSpaceAvailable(poolHandle, size)`.
3. AV Buffer calls `IAVBufferSpaceListener.onSpaceAvailable()` when there is enough free space.
4. Client retries allocation.

This AV Buffer backpressure and decoder backpressure can occur simultaneously:
- AV Buffer out-of-memory prevents new coded buffers from being created.
- Decoder “decode buffer full” prevents coded buffers from being accepted even if they can be allocated.

Deep links:
- AIDL: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- AIDL: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBufferSpaceListener.aidl`
- AV Buffer HALIF docs (handling out-of-memory): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

### Handle ownership transfer points

The control flow defines specific ownership edges:

1. Client -> Decoder: coded input buffer handles on successful `decodeBuffer(...)`.
2. Decoder -> AV Buffer: coded input handles freed after consumption, and also freed during stop/flush for queued but unprocessed inputs.
3. Decoder -> Client: decoded output frame handles returned by `onFrameOutput(...)` in non-tunnelled mode.
4. Client -> AV Buffer: decoded output frame handles freed when downstream is finished (non-tunnelled mode).

A key invariant from the AV Buffer model is that buffer handles live in a single global handle namespace, so freeing is not restricted to the session that allocated the handle. This is what makes the “cross-component free” ownership transfer viable.

### Secure vs non-secure interactions (control-flow impact)

Secure decode is selected at open time (`open(codec, secure, ...)`) and is coupled to secure heap usage on the AV Buffer side (`createVideoPool(true, ...)`).

Control-flow implications include:
- Clients should not mix secure and non-secure coded input handles within a session.
- Secure buffers generally cannot be mapped directly by unprivileged clients; helper mechanisms for writing to secure handles are platform/vendor specific, but the helper interface in this workspace explicitly models “write to secure handle” as an operation that may exist (`IAVBufferHelper.writeSecureHandle(...)` returning false by default).

Deep links:
- AIDL open secure flag: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`
- AV Buffer helper interface: `rdk-halif-aidl-17/avbuffer/current/avbufferhelper.h`
- AV Buffer HALIF docs (secure heaps): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

## Error/edge cases and recovery

### Decoder backpressure: `decodeBuffer(...)` returns false

The most common non-fatal “control-flow disruption” is `decodeBuffer(...)` returning `false` because the decoder’s internal buffer is full.

Typical contributing conditions include:
- Internal coded-input queue depth reached.
- In non-tunnelled mode, decoded-frame pool exhaustion because the client/downstream has not freed output frame handles quickly enough.
- Tunnelled pipeline congestion (downstream vendor path), indirectly throttling acceptance of input.

Recovery is generally:
- Retry later with pacing.
- Ensure decoded output handles are freed promptly (non-tunnelled).
- Consider `flush(reset=false)` if the pipeline is stuck due to a discontinuity or internal drain behavior.

Deep links:
- AIDL `decodeBuffer(...)` return semantics: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`
- Video Decoder HALIF docs (output pool/backpressure narrative): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

### AV Buffer out-of-memory during `alloc()`

When `IAVBuffer.alloc()` fails with `HALError::OUT_OF_MEMORY`, the correct recovery is to use `notifyWhenSpaceAvailable(...)` and wait for `onSpaceAvailable()` before retrying.

A frequent control-flow root cause is a buffer lifecycle leak: coded input buffers are allocated and not freed either because they were never submitted (client retained them) or because the decoder session is stuck and not consuming/freing accepted buffers. Another common cause is downstream congestion causing output handles to be held too long, which in turn causes coded inputs to accumulate.

Deep links:
- AIDL alloc/notify: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- AV Buffer HALIF docs (out-of-memory): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

### Stop/flush as a mandatory buffer release boundary

`stop()` and `flush(reset)` both require that coded input buffers accepted but not yet decoded are automatically freed. This is a control-flow guarantee clients can rely on for recovery and teardown.

Practical implication:
- If a client has submitted buffers and needs to reclaim pool capacity immediately, calling `flush(...)` or `stop()` provides a defined way to force release of queued inputs.

Deep links:
- AIDL stop/flush buffer-free statements: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`

### Closing while started / state violations

The resource `close(controller)` requires `READY`. If a client attempts to close while started, it should first stop and wait for `READY` before calling close.

Violation patterns (and how they show up):
- `open(...)` when not `CLOSED` can throw `EX_ILLEGAL_STATE`.
- `close(...)` when not `READY` can throw `EX_ILLEGAL_STATE` or return `false` depending on implementation (the AIDL documents the precondition and also documents `false` return as “invalid state or unrecognised parameter”).

Recovery is to re-align with the state machine: stop, wait for READY, then close.

Deep links:
- AIDL `open/close` preconditions: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`

### Client crash and implicit cleanup

The `IVideoDecoder.open(...)` contract states that if the client crashes, the controller has `stop()` and `close()` implicitly called to perform cleanup.

Control-flow implications:
- Server implementations should ensure that implicit stop/close performs the same buffer freeing as explicit stop/close.
- Clients should assume that after crash/restart, any previously submitted handles are no longer usable and may already be freed.

Deep links:
- AIDL crash-safety statement: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`

### Pool teardown ordering and NOT_EMPTY failures

`IAVBuffer.destroyPool(poolHandle)` requires the pool to be empty; otherwise it throws `HALError::NOT_EMPTY`.

A correct teardown sequence is therefore:
1. Stop/close the decoder session (so it stops holding coded inputs).
2. Ensure all client-held coded input handles that were never submitted are freed.
3. In non-tunnelled mode, ensure all client/downstream-held decoded output frame handles are freed.
4. Destroy the pool.

Deep links:
- AIDL destroyPool NOT_EMPTY semantics: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- AV Buffer HALIF docs (audio & video frame pools and client-held handles): https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

## References

### Upstream HALIF documentation (deep links)

1. Video Decoder HALIF (current)  
   https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

2. AV Buffer HALIF (current)  
   https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

### Local repository sources used to ground this document

1. Video Decoder AIDL resource interface  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`

2. Video Decoder AIDL controller interface  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`

3. Video Decoder controller callback interface  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderControllerListener.aidl`

4. Video Decoder event listener interface  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderEventListener.aidl`

5. Common HAL state enum used for state transitions  
   `rdk-halif-aidl-17/common/current/com/rdk/hal/State.aidl`

6. AV Buffer AIDL interfaces  
   `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`  
   `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBufferSpaceListener.aidl`

7. AV Buffer helper interface (mapping/unmapping and secure write helpers)  
   `rdk-halif-aidl-17/avbuffer/current/avbufferhelper.h`
