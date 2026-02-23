<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder/sequence_diagram.md
- Operation: write
- Timestamp: 2026-02-10T12:55:13.512181+00:00
- Restored At: 2026-02-23T05:04:11.251527+00:00
- Task ID: cm219d4578
-->

# Video Decoder Pipeline Sequence Diagrams (HALIF AIDL)

## Overview

This document provides **readable, non-Mermaid ASCII sequence diagrams** for the HALIF AIDL Video Decoder pipeline. The sequences are grounded in the upstream HALIF specifications for:

The Video Decoder HAL:
- `IVideoDecoderManager`, `IVideoDecoder`, `IVideoDecoderController`
- `IVideoDecoderEventListener`, `IVideoDecoderControllerListener`

And the AV Buffer HAL:
- `IAVBuffer`, `Pool`, `IAVBufferSpaceListener`

These diagrams focus on the runtime call ordering, buffer-handle ownership transfer, and the most important alternate/error paths (backpressure, invalid handles, teardown ordering, and flush/stop freeing behavior).

Unless stated otherwise, “Client” refers to middleware (for example, an RDK video pipeline element) that allocates coded input buffers, feeds the decoder, and consumes output callbacks.

## Legend

The following legend is used in the diagrams:

- `->` indicates a synchronous AIDL call from caller to callee.
- `<-` indicates a return value to the caller (when meaningful).
- `=>` indicates an asynchronous oneway callback from server to client.
- `[handle]` indicates an AV Buffer `long` handle (globally unique).
- `[pool]` indicates an AV Buffer pool handle (`Pool` parcelable).
- Notes describe ownership and lifecycle rules that are normative in the AIDL comments.

## 1) Typical Video Decoder session sequence

### 1.1 Typical non-tunnelled session (coded handles in, decoded frame handles out)

This is the “full” lifecycle including resource selection, pool creation, open/start, steady-state decode, EOS, stop/close, and pool destruction.

Key ownership edges:
1. The client allocates coded input handles.
2. On successful `decodeBuffer(...)`, ownership of the coded input handle transfers to the decoder.
3. The decoder frees coded input handles after consumption, returning space to the AV Buffer pool.
4. In non-tunnelled mode, the decoder returns decoded frame handles to the client via `onFrameOutput(...)`.
5. The client (or downstream pipeline component) frees decoded frame handles when finished.

```
Participants:
  Client                  VideoDecoderMgr      VideoDecoder        DecoderCtrl        CtrlListener        EventListener        AVBuffer            SpaceListener
  (middleware)            (IVideoDecoderMgr)   (IVideoDecoder)     (IVideoDecoderCtl) (IVideoDecoderCtlL) (IVideoDecoderEvtL)  (IAVBuffer)         (IAVBufferSpaceL)

1) Discover decoder resources
  Client -> VideoDecoderMgr: getVideoDecoderIds()
  Client <- VideoDecoderMgr: ids[]
  Client -> VideoDecoderMgr: getSupportedOperationalModes()
  Client <- VideoDecoderMgr: modes[]
  Client -> VideoDecoderMgr: getVideoDecoder(id)
  Client <- VideoDecoderMgr: IVideoDecoder (or null if invalid)

2) Create AV Buffer pool for coded input
  Client -> AVBuffer: createVideoPool(secureHeap, videoDecoderId, SpaceListener)
  Client <- AVBuffer: Pool [pool]  (or Pool.INVALID_POOL on failure)

3) Register for state/errors (optional but typical)
  Client -> VideoDecoder: registerEventListener(EventListener)
  Client <- VideoDecoder: true/false

4) Open session
  Client -> VideoDecoder: open(codec, secure, CtrlListener)
  VideoDecoder => EventListener: onStateChanged(CLOSED, OPENING)
  VideoDecoder => EventListener: onStateChanged(OPENING, READY)
  Client <- VideoDecoder: DecoderCtrl (or null if codec/secure unsupported)

5) Start
  Client -> DecoderCtrl: start()
  VideoDecoder => EventListener: onStateChanged(READY, STARTING)
  VideoDecoder => EventListener: onStateChanged(STARTING, STARTED)

6) Steady-state decode loop (per coded frame)
  Client -> AVBuffer: alloc([pool], size)
  Client <- AVBuffer: codedHandle [H_in]

  (Client fills codedHandle payload out-of-band; mapping vs secure write is AV Buffer/implementation dependent)

  Client -> DecoderCtrl: decodeBuffer(ptsNs, [H_in])
  Client <- DecoderCtrl: true or false

  If decodeBuffer() returned true:
    NOTE: Ownership of [H_in] transfers to the decoder. Client must not modify/free [H_in].

    DecoderCtrl => CtrlListener: onFrameOutput(ptsNs, frameHandle [H_out], metadataOrNull)
      NOTE: In non-tunnelled mode, [H_out] is a decoded frame handle. Client must eventually free it.
      NOTE: metadata must be non-null on first frame after start() or flush(), or when metadata changes.

    Decoder (internally) -> AVBuffer: free([H_in])
      NOTE: This is the normative “coded buffers are automatically released and returned to AV Buffer” behavior.

  If decodeBuffer() returned false:
    NOTE: Decoder backpressure (“decode buffer is full”). Ownership does NOT transfer.
    NOTE: Client retains [H_in] and may retry later or free/reuse according to its buffering strategy.

7) End of stream and drain
  Client -> DecoderCtrl: signalEOS()
  NOTE: No more coded buffers are expected after EOS unless flush/stop + start is performed.
  DecoderCtrl => CtrlListener: onFrameOutput(lastPtsOr-1, [H_out or -1], metadata(endOfStream=true))

8) Stop and close
  Client -> DecoderCtrl: stop()
  VideoDecoder => EventListener: onStateChanged(STARTED, STOPPING)
  VideoDecoder => EventListener: onStateChanged(STOPPING, READY)

  Client -> VideoDecoder: close(DecoderCtrl)
  VideoDecoder => EventListener: onStateChanged(READY, CLOSING)
  VideoDecoder => EventListener: onStateChanged(CLOSING, CLOSED)
  Client <- VideoDecoder: true/false

9) Teardown AV Buffer pool (requires pool empty)
  Client -> AVBuffer: destroyPool([pool])
  Client <- AVBuffer: true (or EX_SERVICE_SPECIFIC/HALError::NOT_EMPTY if outstanding allocations remain)

10) Unregister events (optional)
  Client -> VideoDecoder: unregisterEventListener(EventListener)
  Client <- VideoDecoder: true/false
```

### 1.2 Typical tunnelled session (no decoded frame handles)

Tunnelled mode differs primarily in output: decoded frames are consumed within the vendor pipeline and not returned as AV buffer handles. Metadata updates may still be delivered; when metadata is delivered without a frame handle, `frameBufferHandle = -1` is used.

```
Participants:
  Client            VideoDecoder            DecoderCtrl            CtrlListener            AVBuffer

1) Setup is the same: createVideoPool(), open(), start().

2) Steady-state decode loop
  Client -> AVBuffer: alloc([pool], size)
  Client <- AVBuffer: codedHandle [H_in]

  Client -> DecoderCtrl: decodeBuffer(ptsNs, [H_in])
  Client <- DecoderCtrl: true/false

  If accepted (true):
    Decoder (internally) -> AVBuffer: free([H_in])

    DecoderCtrl => CtrlListener: onFrameOutput(ptsNs or -1, -1, metadataOrNull)
      NOTE: In tunnelled mode there is no decoded frame handle returned.
      NOTE: onFrameOutput() may be omitted entirely when operating exclusively tunnelled and there is no metadata update needed.

3) stop()/close() and destroyPool() ordering is unchanged.
```

## 2) AV Buffer pool/handle lifecycle as used by Video Decoder

This section focuses on the AV Buffer objects and constraints that are most relevant to the decoder pipeline.

### 2.1 Pool creation and allocation/free loop

The key constraints are:
1. Pools are created per heap type (secure/non-secure) and are associated with a specific decoder ID for accounting/sizing.
2. Allocation is immediate-or-fail. If out-of-memory occurs, the specified recovery is `notifyWhenSpaceAvailable(...)` followed by `IAVBufferSpaceListener.onSpaceAvailable()`.
3. Handles are globally unique, and `free(handle)` is valid even when a different component owns the handle (this enables “client allocates, decoder frees” and vice versa).
4. `destroyPool(pool)` requires the pool to be empty; otherwise `HALError::NOT_EMPTY` is raised as a service-specific exception.

```
Participants:
  Client                AVBuffer                 SpaceListener                 VideoDecoder (conceptual)

1) Create pool (listener lifetime must cover pool lifetime)
  Client -> AVBuffer: createVideoPool(secureHeap, videoDecoderId, SpaceListener)
  Client <- AVBuffer: Pool [pool]

  NOTE: SpaceListener must remain valid until after destroyPool([pool]) returns.

2) Allocate coded input handles
  Client -> AVBuffer: alloc([pool], size)
  Client <- AVBuffer: codedHandle [H_in]

3) Transfer codedHandle to decoder on successful decodeBuffer()
  Client -> VideoDecoder: decodeBuffer(ptsNs, [H_in])
  Client <- VideoDecoder: true

  NOTE: Ownership transfers to Video Decoder for [H_in].

4) Decoder frees codedHandle after consumption
  VideoDecoder -> AVBuffer: free([H_in])
  VideoDecoder <- AVBuffer: true

5) Pool teardown requires all outstanding handles freed
  Client -> AVBuffer: destroyPool([pool])
  Client <- AVBuffer: true
```

### 2.2 Decoded output frame handles (non-tunnelled) in the global handle namespace

In non-tunnelled mode, the decoder outputs `frameBufferHandle` values via `onFrameOutput(...)`. These handles are still freed through the same AV Buffer service:

```
Participants:
  VideoDecoder            CtrlListener / Client            Downstream (e.g. Video Sink)            AVBuffer

1) Output decoded frame handle
  VideoDecoder => CtrlListener: onFrameOutput(ptsNs, frameHandle [H_out], metadataOrNull)

2) Downstream consumes
  Client -> Downstream: queue/present [H_out] (out of scope of Video Decoder HAL)

3) Release when finished
  Downstream/Client -> AVBuffer: free([H_out])
  Downstream/Client <- AVBuffer: true
```

This cross-component freeing works because AV Buffer handles are required to be globally unique and free-able by any client.

## 3) Error and alternate paths

This section captures the most important “alt paths” that affect ordering and cleanup. These are not vendor-implementation details; they are driven by the AIDL contract and the upstream HALIF docs.

### 3.1 Backpressure path A: Decoder-side backpressure (decodeBuffer returns false)

`IVideoDecoderController.decodeBuffer(...)` returns `false` when the “decode buffer is full.” In this case, the decoder did not accept ownership of the coded handle.

```
Participants:
  Client              DecoderCtrl

  Client -> DecoderCtrl: decodeBuffer(ptsNs, codedHandle [H_in])
  Client <- DecoderCtrl: false

  NOTE: Ownership does not transfer. Client must not assume the decoder will free [H_in].
  NOTE: Typical strategy is to retry later with pacing, or apply upstream throttling.
```

A common root cause in non-tunnelled mode is output-frame pool exhaustion when the client/downstream is holding decoded frames too long. Another root cause is internal coded-input queue depth limits.

### 3.2 Backpressure path B: AV Buffer pool exhaustion (alloc throws OUT_OF_MEMORY)

When `IAVBuffer.alloc(...)` fails due to pool exhaustion, it raises a service-specific error `HALError::OUT_OF_MEMORY` (as an exception). The intended recovery is notification via `notifyWhenSpaceAvailable(...)`.

```
Participants:
  Client                 AVBuffer                  SpaceListener

  Client -> AVBuffer: alloc([pool], size)
  Client <- AVBuffer: EX_SERVICE_SPECIFIC(HALError::OUT_OF_MEMORY)

  Client -> AVBuffer: notifyWhenSpaceAvailable([pool], size)
  Client <- AVBuffer: true

  ...later...
  AVBuffer => SpaceListener: onSpaceAvailable()

  Client NOTE: Retry alloc() only after callback (or after the client has reason to believe space returned).
```

This path typically indicates either:
1. The client is holding too many coded input handles, or
2. The decoder is holding accepted coded handles and has not yet freed them (for example due to congestion), or
3. The pool is undersized for the workload.

### 3.3 Invalid handles and invalid IDs

#### 3.3.1 Invalid videoDecoderId on pool creation

`createVideoPool(secureHeap, videoDecoderId, listener)` requires the `videoDecoderId` to have been obtained from `IVideoDecoderManager.getVideoDecoderIds()`. If not, the call fails with `EX_ILLEGAL_ARGUMENT`.

```
Client -> AVBuffer: createVideoPool(secureHeap, invalidVideoDecoderId, SpaceListener)
Client <- AVBuffer: EX_ILLEGAL_ARGUMENT
```

#### 3.3.2 Invalid pool on alloc() or invalid handle on free()

`alloc(pool, size)` returns `INVALID_HANDLE` for invalid pool handles or sizes larger than pool size. `free(handle)` returns `false` if the handle is invalid.

```
Client -> AVBuffer: alloc(invalidPool, size)
Client <- AVBuffer: INVALID_HANDLE (-1)

Client -> AVBuffer: free(invalidHandle)
Client <- AVBuffer: false
```

`isValid(handle)` can be used to check handle validity for debugging/defensive behavior.

### 3.4 Pool not empty on destroy (NOT_EMPTY) and correct teardown ordering

`destroyPool(pool)` can only succeed if the pool is empty. If any buffer allocations are outstanding, it throws `EX_SERVICE_SPECIFIC(HALError::NOT_EMPTY)`.

A typical failure occurs when:
- The client still holds coded input handles that were allocated but never successfully transferred to the decoder, or
- The client/downstream still holds decoded output frame handles (non-tunnelled mode), or
- The decoder still holds coded input handles (for example, if the session was not properly stopped/closed).

```
Participants:
  Client                 AVBuffer                 VideoDecoder

  Client -> AVBuffer: destroyPool([pool])
  Client <- AVBuffer: EX_SERVICE_SPECIFIC(HALError::NOT_EMPTY)

  NOTE: To fix, ensure all handles allocated from [pool] are freed.
  NOTE: In non-tunnelled mode, also ensure all decoded output handles [H_out] are freed.
  NOTE: Ensure the decoder session is stopped/closed so it is no longer holding accepted coded buffers.
```

A robust teardown ordering is:
1. `stop()` (if started) and wait for READY via state callbacks.
2. `close(controller)`.
3. Free any client-held coded inputs that were never accepted (decodeBuffer returned false).
4. Free any client/downstream-held decoded output handles (non-tunnelled).
5. `destroyPool(pool)`.

### 3.5 Flush/stop freeing behavior (mandatory buffer release boundary)

Both `flush(reset)` and `stop()` include an explicit guarantee in the controller AIDL:

- On `stop()`: any input buffers passed for decode but not yet decoded are automatically freed.
- On `flush(reset)`: any input buffers passed for decode but not yet decoded are automatically freed, and pending decoded frames due for callback are returned to the output frame pool.

This is one of the most important lifecycle guarantees, because it provides a defined “release boundary” for recovery and teardown.

#### 3.5.1 Flush frees queued coded inputs

```
Participants:
  Client                DecoderCtrl              VideoDecoder (internal)            AVBuffer

  (Assume STARTED, and decoder may have accepted multiple coded handles [H_in...] but not yet fully decoded them.)

  Client -> DecoderCtrl: flush(reset=false or true)

  NOTE: All coded input buffers accepted but not yet decoded are freed as part of flush.

  VideoDecoder (internal) -> AVBuffer: free([H_in_1])
  VideoDecoder (internal) -> AVBuffer: free([H_in_2])
  ...
  (Flush completes; state transition may be observable via EventListener as FLUSHING -> STARTED)
```

After flush, the metadata rules require that the first output callback after `flush()` carries non-null metadata.

#### 3.5.2 Stop frees queued coded inputs and returns to READY

```
Participants:
  Client                DecoderCtrl              VideoDecoder (internal)            AVBuffer            EventListener

  Client -> DecoderCtrl: stop()
  VideoDecoder => EventListener: onStateChanged(STARTED, STOPPING)

  NOTE: All coded input buffers accepted but not yet decoded are freed during STOPPING.

  VideoDecoder (internal) -> AVBuffer: free([H_in_1])
  VideoDecoder (internal) -> AVBuffer: free([H_in_2])
  ...

  VideoDecoder => EventListener: onStateChanged(STOPPING, READY)
```

### 3.6 Client crash / abnormal termination cleanup (implicit stop + close)

The `IVideoDecoder.open(...)` contract states that if the client that opened the controller crashes, then `stop()` and `close()` are implicitly called to perform cleanup. From a buffer lifecycle perspective, the intended effect is “as if stop/close happened,” including freeing any accepted coded handles held by the decoder.

```
Participants:
  Client process         VideoDecoder service          DecoderCtrl           AVBuffer

  Client process terminates unexpectedly
  VideoDecoder service detects binder death (conceptual)

  VideoDecoder service (implicit) performs:
    - stop() semantics (free queued coded inputs)
    - close() semantics (release session)

  VideoDecoder service -> AVBuffer: free([H_in held by decoder])
```

## 4) References (deep links)

### Upstream HALIF documentation

1. Video Decoder HALIF documentation (current)  
   https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

2. AV Buffer HALIF documentation (current)  
   https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

### Local repository sources used to ground this document

1. Video Decoder AIDL interfaces  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderManager.aidl`  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderController.aidl`  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderEventListener.aidl`  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoderControllerListener.aidl`

2. AV Buffer AIDL interfaces  
   `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`  
   `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBufferSpaceListener.aidl`  
   `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/Pool.aidl`

3. Existing local videodecoder pipeline docs used for consistency  
   `kavia-docs/videodecoder/pipeline_components.md`  
   `kavia-docs/videodecoder/pipeline_control_flow.md`  
   `kavia-docs/videodecoder/pipeline_dataflow.md`  
   `kavia-docs/videodecoder/pipeline_buffer_lifecycle.md`
