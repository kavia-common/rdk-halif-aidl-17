<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder_documents/video-decoder-control-flow.md
- Operation: write
- Timestamp: 2026-02-12T04:44:08.056085+00:00
- Restored At: 2026-02-23T05:04:11.251881+00:00
- Task ID: cm219d4578
-->

# Video Decoder Control Flow (HALIF AIDL)

## Scope and references

This document describes the Video Decoder control flow using only the content from the following references.

- Video Decoder states and related behavior: https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/video_decoder/current/video_decoder/#video-decoder-states
- AV Buffer concepts used by the decoder flow: https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/av_buffer/current/av_buffer/
- Video decoder AIDL interface definition files list: https://github.com/rdkcentral/rdk-halif-aidl/tree/main/videodecoder/current/com/rdk/hal/videodecoder

## Interfaces involved (from the AIDL interface definition list)

The Video Decoder HAL is structured around these interfaces.

The manager-level entry point is `IVideoDecoderManager.aidl`, which provides access to `IVideoDecoder` resource instances. A single decoder resource is represented by `IVideoDecoder.aidl`. A client opens a decoder resource to obtain a controller sub-interface, `IVideoDecoderController.aidl`, which is used for state controls and buffer decode. Callbacks from the controller to the client use `IVideoDecoderControllerListener.aidl`, and events from `IVideoDecoder` use `IVideoDecoderEventListener.aidl`.

## Buffer model used by decodeBuffer() (AV Buffer)

The media pipeline passes encoded video frames to the video decoder using AV buffer handles. AV buffers are allocated from pools created by a client from either secure or non-secure heaps. Handles are used to reference buffers as they move across HAL interfaces, and a handle can be passed to `IAVBuffer.free()` to return the buffer to its original pool.

When a client allocates an AV buffer with `IAVBuffer.alloc()`, it receives a globally unique integer handle. Ownership of the handle can be passed to a HAL component such as a decoder via `IVideoDecoderController.decodeBuffer()`, after which the buffer is eventually freed. The AV Buffer model supports secure and non-secure memory. Secure buffers cannot be mapped into unprivileged processes and are intended to meet secure video path requirements, while non-secure buffers can be mapped for read/write access in middleware processes.

## Control flow overview

A client interacts with the service using a sequence that is centered on opening a decoder resource, starting it, submitting coded frames by handle using `decodeBuffer()`, and receiving frame output callbacks and/or metadata callbacks, followed by flush/stop/close as required.

The Video Decoder HAL uses the standard Session State Management paradigm. When a Video Decoder session enters a FLUSHING or STOPPING transitory state it shall free any AV buffers it is holding.

## Session state transitions (Video Decoder states)

The reference sequence describes the following control flow and state transitions.

A client first registers an `IVideoDecoderEventListener` with `registerEventListener(IVideoDecoderEventListener)`. It then opens the decoder using `open(IVideoDecoderControllerListener)`, which transitions the session from `CLOSED -> OPENING -> READY`. Starting the session with `start()` transitions `READY -> STARTING -> STARTED`. While started, the client can submit compressed frames using `decodeBuffer(pts, bufferHandle=...)`.

A `flush()` transitions `STARTED -> FLUSHING -> STARTED`. During the FLUSHING transitory state, the decoder frees any AV buffers it is holding. A `stop()` transitions `STARTED -> STOPPING -> READY`, and during STOPPING the decoder frees any AV buffers it is holding. Finally, `close()` transitions `READY -> CLOSING -> CLOSED`. The event listener can be removed with `unregisterEventListener(IVideoDecoderEventListener)`.

## Frame output and metadata behavior (onFrameOutput)

Decoded output is conveyed to the client via `IVideoDecoderControllerListener.onFrameOutput()`. As video frames are decoded, the metadata related to frames is passed over `IVideoDecoderControllerListener.onFrameOutput()`. In non-tunnelled mode, the frame buffer handle and the frame metadata are passed in the same `onFrameOutput()` call.

To conserve CPU load, frame metadata is only passed with the first decoded frame after a `start()`, the first decoded frame after a `flush()`, or when the frame metadata changes. When metadata does not need to be passed, the `@nullable FrameMetadata metadata` parameter is passed as null in `onFrameOutput()`.

## Operational modes and how they affect the flow

The advertised operational modes are returned by `IVideoDecoderManager.getSupportedOperationalModes()`. Tunnelled and non-tunnelled modes cannot operate at the same time, and if both are supported there shall never be a dynamic switch between the two modes while `STARTED`. The `OPERATIONAL_MODE` property controls the operational mode. The decoder may switch operational modes at any time while in a `READY` or `STARTED` state.

In all modes, AV buffers containing compressed video are passed into the video decoder through calls to `decodeBuffer()`.

In `TUNNELLED` mode, decoded video frames are passed directly through the vendor layer, and `onFrameOutput()` calls do not return a decoded frame buffer handle. If `onFrameOutput()` must be called to provide updated `FrameMetadata`, the `frameBufferHandle` is set to `-1`. If operating exclusively in tunnelled mode and there is no frame metadata to pass, then no call to `onFrameOutput()` should be made.

In `NON_TUNNELLED` mode, decoded video frames are returned as video frame buffer handles over `onFrameOutput()`, and frames must be received in presentation order. The frame buffer handle is later queued for presentation and then freed. The vendor layer manages the pool of decoded frame buffers and reports its size in the `OUTPUT_FRAME_POOL_SIZE` property. If the output pool is empty then frame output can be blocked, and the decoder may buffer additional coded input buffers or reject new `decodeBuffer()` calls with a false return value.

`GRAPHICS_TEXTURE` mode converts video frames to NV12 textures and can run concurrently with tunnelled or non-tunnelled mode.

## Discontinuity, end-of-stream, and ordering rules

Where the client has knowledge of PTS discontinuities, it calls `IVideoDecoderController.signalDiscontinuity()` between the AV buffers passed to `decodeBuffer()`. The first input AV buffer passed for decode after the discontinuity indicates the discontinuity in its next output `FrameMetadata`.

When the client has delivered the final coded video frame buffer for decode, it calls `IVideoDecoderController.signalEOS()`. The decoder continues decoding previously submitted buffers, and after all frames have been output it emits `FrameMetadata` with `endOfStream=true`.

Presentation time base units are nanoseconds. The `nsPresentationTime` passed into `decodeBuffer()` represents the frame presentation time, and `onFrameOutput()` uses the same `nsPresentationTime`. The video decoder outputs frames in presentation order regardless of the order of input frames.

## AV buffer freeing behavior in flush/stop

When a Video Decoder session enters a `FLUSHING` or `STOPPING` transitory state it frees any AV buffers it is holding. In the reference sequence, after `decodeBuffer(pts, bufferHandle=...)`, the associated coded input buffer handles are freed during flush and stop sequences.
