<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder/pipeline_buffer_lifecycle.md
- Operation: write
- Timestamp: 2026-02-10T12:24:16.806559+00:00
- Restored At: 2026-02-23T05:04:11.251210+00:00
- Task ID: cm219d4578
-->

# Video Decoder Pipeline Buffer Lifecycle (HALIF AIDL)

## Overview

This document describes the lifecycle of buffers as they move through the RDK HALIF AIDL video decoding pipeline, focusing on how compressed (coded) video frames are carried in AV Buffer handles into the Video Decoder HAL, and how decoded frames are produced and ultimately released.

The lifecycle is defined by the interaction between two HALIF services:

1. The Video Decoder HAL, which accepts compressed frames (by handle) and produces decoded frame output in either tunnelled or non-tunnelled mode.
2. The AV Buffer HAL, which provides secure and non-secure heaps, pools, and globally unique buffer handles that can be passed across HALs and freed from any client session.

In general, coded input is provided to the decoder as AV buffer handles passed to `IVideoDecoderController.decodeBuffer()`. Ownership of these handles is transferred to the decoder, and the decoder is responsible for freeing them when it has finished consuming them. In non-tunnelled mode, the decoder returns decoded frame buffer handles back to the client via `IVideoDecoderControllerListener.onFrameOutput()`, and those decoded frame handles are eventually freed when the downstream pipeline is done with them.

## Buffers Lifecycle

### Buffer types and where they originate

The pipeline uses two main handle types that both live in the global AV Buffer handle space:

1. Compressed input buffers (coded video frames). These are allocated by a client (typically RDK middleware) from an AV Buffer pool and filled with elementary stream frame data. They may be secure or non-secure.
2. Decoded output frame buffers (non-tunnelled mode only). These are allocated and managed privately in the vendor layer by the video decoder as part of a vendor-managed output frame pool. The client receives handles for these buffers and later frees them.

Because the AV Buffer HAL requires global uniqueness of handles across all heaps and pools, including the vendor audio/video frame pools, a handle returned from a decoder for a decoded frame can be released using the standard `IAVBuffer.free()` API.

### High-level lifecycle phases

The lifecycle can be described as a repeating loop of four phases:

1. Allocate
2. Submit
3. Decode
4. Release

Each phase occurs for coded input buffers, and (in non-tunnelled mode) for decoded output buffers as well.

### Allocate

#### Allocate coded input buffers (client responsibility)

A client allocates coded frame buffers from an AV Buffer pool:

- The client creates a pool using `IAVBuffer.createVideoPool(secureHeap, videoDecoderId, listener)`.
- For each coded frame, the client allocates a buffer using `IAVBuffer.alloc(poolHandle, size)`.

For non-secure buffers, the client can map/unmap the handle using the AV Buffer helper library (described in the upstream AV Buffer documentation) and copy frame bytes into the buffer.

Secure buffers are not mappable into unprivileged processes. The vendor layer typically provides mechanisms to copy into secure buffers without exposing raw memory to the client process.

#### Allocate decoded output buffers (vendor responsibility)

In non-tunnelled mode, decoded output buffers are allocated from a vendor-managed output frame pool. The Video Decoder documentation describes this as a pool “managed privately” by the vendor layer, with the pool size reported using the `OUTPUT_FRAME_POOL_SIZE` property.

If the output frame pool is empty, the decoder cannot output the next decoded frame until a frame buffer becomes available again. In this condition, the decoder may buffer additional coded input or reject additional `decodeBuffer()` calls (for example, by returning `false`).

### Submit

The client submits coded buffers to the decoder:

- The video decoder is opened with `IVideoDecoder.open(codec, secure, listener)` which returns an `IVideoDecoderController`.
- The client transitions the decoder to `STARTED` using `IVideoDecoderController.start()`.
- For each coded frame, the client calls `IVideoDecoderController.decodeBuffer(nsPresentationTime, bufferHandle)`.

At this point, the coded AV buffer handle ownership is considered transferred to the decoder. The buffer must not be freed by the client unless the call failed and ownership was not taken (the precise failure semantics are implementation dependent; clients should follow the HAL’s return values and error callbacks).

### Decode

The decoder processes coded inputs and produces decoded outputs.

#### Output behavior: tunnelled vs non-tunnelled

- In tunnelled mode, decoded frames are passed directly through the vendor layer to the linked sink/plane for rendering. The client does not receive decoded frame buffer handles back. The decoder still provides `FrameMetadata` updates (and uses `frameBufferHandle = -1` when metadata must be sent via `onFrameOutput()` without an associated frame buffer handle).
- In non-tunnelled mode, decoded frames are returned to the client via `IVideoDecoderControllerListener.onFrameOutput(nsPresentationTime, frameBufferHandle, metadata)`.

#### Frame metadata emission rules (key for lifecycle timing)

To conserve CPU load, metadata is not necessarily returned with every frame. The upstream Video Decoder documentation specifies that metadata is passed:

1. With the first decoded frame after `start()`.
2. With the first decoded frame after `flush()`.
3. When metadata changes.

If metadata does not need to be passed, the `metadata` parameter may be `null`. In tunnelled-only operation, when there is no frame handle and no metadata update is needed, `onFrameOutput()` should not be called.

### Release

Release is the key lifecycle step that returns finite resources back to pools/heaps.

#### Release coded input buffers (decoder responsibility)

After consuming a coded input buffer, the video decoder must free it. The upstream sequence diagram explicitly shows the decoder calling `IAVBuffer.free(bufferHandle)` after output.

Additionally, when entering certain transitory states, the decoder must free any AV buffers it is holding. The upstream Video Decoder documentation states:

- When the session enters `FLUSHING` or `STOPPING`, it shall free any AV buffers it is holding.

This prevents coded buffers from leaking and ensures pool space becomes available again.

#### Release decoded output frame buffers (client responsibility in non-tunnelled mode)

In non-tunnelled mode, the client receives decoded output frame buffer handles and passes them downstream (typically to Video Sink). When the downstream pipeline is done with a decoded frame handle, it must be freed via `IAVBuffer.free(frameBufferHandle)`.

The AV Buffer documentation highlights an important shutdown case: when an AV pipeline is stopped or flushed, the client may still be holding decoded frame handles; in that case, the client must free them explicitly.

## AV Buffer Interaction

### Pools, heaps, and security

AV Buffer provides two heap types:

1. Non-secure heap (`secureHeap=false`) for encrypted or clear data that does not require a secure path.
2. Secure heap (`secureHeap=true`) for secure SoC memory that cannot be mapped into untrusted processes.

Clients create video pools (secure or non-secure) targeted to a specific video decoder resource ID using `createVideoPool()`. The vendor layer sizes pools according to platform capability and product needs.

### Handle properties and cross-component freeing

The AV Buffer requirements and system context emphasize that:

1. AV buffer handles must be globally unique across all pools and heaps, including the vendor video/audio frame pools.
2. Any handle can be freed via `IAVBuffer.free()` to return it to its original pool, and frees are not restricted to the client session that allocated it.

This is what allows a lifecycle in which:

- The client allocates coded buffers, the decoder frees them.
- The decoder allocates decoded frame buffers, the client frees them.

### Out-of-memory behavior and space notifications

Pools are finite. When allocation fails due to exhaustion, `IAVBuffer.alloc()` returns an out-of-memory service-specific error, and the client can call:

- `IAVBuffer.notifyWhenSpaceAvailable(poolHandle, size)`

The AV Buffer service will later invoke:

- `IAVBufferSpaceListener.onSpaceAvailable()`

This mechanism is a central part of robust buffer lifecycle management because it provides backpressure signals to the pipeline.

## Typical Flows

### Flow 1: Non-tunnelled decoding (coded in, decoded frame handles out)

1. Client obtains `IAVBuffer` service.
2. Client selects a video decoder resource ID (from the decoder manager) and creates a video pool with `createVideoPool()`.
3. Client allocates coded input buffers with `alloc()` and fills them.
4. Client opens the decoder with `IVideoDecoder.open(codec, secure, controllerListener)` and starts the controller.
5. Client submits coded buffers via `decodeBuffer(pts, handle)`.
6. Decoder produces decoded frames and calls `onFrameOutput(pts, frameBufferHandle, metadataOrNull)`.
7. Decoder frees coded input handles once consumed by calling `IAVBuffer.free(inputHandle)`.
8. Client passes decoded output handles downstream (for example, to a sink) and eventually frees them using `IAVBuffer.free(frameBufferHandle)`.

### Flow 2: Tunnelled decoding (coded in, no decoded frame handles out)

1. Client allocates coded input buffers as normal and submits them to `decodeBuffer()`.
2. Decoder renders (or forwards) decoded video frames within vendor layer.
3. Decoder frees coded input handles after consumption.
4. Decoder may still emit `FrameMetadata` updates via `onFrameOutput()`, using `frameBufferHandle = -1` to indicate “no frame handle”.

### Flow 3: Flush (release held input handles, restart decoding)

A flush resets decode pipeline state but does not require closing the decoder session.

1. Client calls `IVideoDecoderController.flush()`.
2. Decoder enters `FLUSHING` and must free any coded AV buffers it is holding.
3. Decoder transitions back to `STARTED`.
4. Client resumes calling `decodeBuffer()`.

Flush is an important lifecycle boundary because it is an explicit guarantee that buffers being held by the decoder will be released, enabling upstream pools to recover capacity.

### Flow 4: Stop (release held input handles, transition to READY)

1. Client calls `IVideoDecoderController.stop()`.
2. Decoder enters `STOPPING` and must free any coded AV buffers it is holding.
3. Decoder transitions to `READY`.
4. Client may `start()` again or `close()`.

### Flow 5: End-of-stream (EOS) signaling

1. Client sends the final coded frame buffer(s) via `decodeBuffer()`.
2. Client calls `IVideoDecoderController.signalEOS()`.
3. Decoder continues to decode all previously submitted buffers.
4. After all frames are output, decoder emits `FrameMetadata` with `endOfStream=true`.

This flow affects lifecycle timing because after `signalEOS()`, no more coded buffers should be submitted unless the pipeline is restarted or flushed.

## Error Paths

### Out-of-memory / output backpressure

There are two common backpressure points:

1. AV Buffer pool exhaustion. `IAVBuffer.alloc()` may fail with out-of-memory; the client should pause feeding and optionally use `notifyWhenSpaceAvailable()` to resume when space returns.
2. Decoder output frame pool exhaustion (non-tunnelled). If the vendor’s decoded frame pool is empty, the decoder cannot output new frames and may either:
   1. Internally queue more coded buffers, or
   2. Reject new `decodeBuffer()` calls (for example, returning `false`), forcing the client to retry later.

In both cases, the underlying resolution is the same: some buffers must be released (freed) so that pools can provide capacity again.

### Stream discontinuities

When the client knows there is a PTS discontinuity between coded frames, it calls:

- `IVideoDecoderController.signalDiscontinuity()`

The next output `FrameMetadata` is expected to indicate the discontinuity. From a lifecycle standpoint, discontinuity does not inherently change buffer ownership rules, but it changes how downstream components interpret frame timing and may cause internal decoder flushing behavior.

### Stop/flush transitory states (mandatory release)

The upstream Video Decoder documentation explicitly requires that in `FLUSHING` and `STOPPING`, the decoder frees any AV buffers it holds. This is a lifecycle guarantee that prevents resource leaks when control flow changes abruptly.

### Client crash / abnormal termination

The Video Decoder AIDL comments describe a failure-handling guarantee:

- If the client that opened the `IVideoDecoderController` crashes, the controller has `stop()` and `close()` implicitly called to perform cleanup.

In practical terms, this implies the vendor service should behave similarly to explicit stop/close, including releasing any buffers it holds and returning resources to a stable state.

### Destroying pools while buffers are still outstanding

The AV Buffer HAL requires that `destroyPool()` may only succeed if the pool is empty. If any allocations are outstanding, it returns a service-specific error (`HALError::NOT_EMPTY`). This forces correct lifecycle cleanup:

1. Ensure all coded input handles allocated from the pool have been freed (either by the client or by downstream HAL components that took ownership).
2. Ensure any derived handles (including decoded frame handles) have also been freed if they are tied to vendor pools that share the global handle space.

## References

### Upstream HALIF docs

1. Video Decoder HALIF (documentation page)  
   https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/video_decoder/current/video_decoder/

2. AV Buffer HALIF (documentation page)  
   https://rdkcentral.github.io/rdk-halif-aidl/0.12.0/halif/av_buffer/current/av_buffer/

### Repository sources used for grounding

1. Video Decoder HALIF markdown (local workspace copy)  
   `rdk-halif-aidl-17/docs/halif/video_decoder/current/video_decoder.md`

2. AV Buffer HALIF markdown (local workspace copy)  
   `rdk-halif-aidl-17/docs/halif/av_buffer/current/av_buffer.md`

3. AIDL interface definition: `IVideoDecoder`  
   `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/IVideoDecoder.aidl`

4. AIDL interface definition: `IAVBuffer`  
   `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
