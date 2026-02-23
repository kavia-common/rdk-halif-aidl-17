<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder_documents/video-decoder-pipeline-components.md
- Operation: write
- Timestamp: 2026-02-12T04:42:10.109108+00:00
- Restored At: 2026-02-23T05:04:11.251831+00:00
- Task ID: cm219d4578
-->

# Video Decoder Pipeline Components (HALIF AIDL)

## Scope and references

This document describes the Video Decoder pipeline components as defined by the HALIF AIDL documentation and interface definitions.

References used:
- Video Decoder HALIF documentation (includes Video Decoder States section): https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/video_decoder/current/video_decoder/#video-decoder-states
- AV Buffer HALIF documentation: https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/av_buffer/current/av_buffer/
- Video decoder AIDL interface definitions (tree): https://github.com/rdkcentral/rdk-halif-aidl/tree/main/videodecoder/current/com/rdk/hal/videodecoder

## Components and responsibilities

The Video Decoder HAL provides interfaces for passing compressed video to the vendor layer for decoding. The decoded output can follow either non-tunnelled mode, where decoded frames are returned to the client as frame buffer handles plus metadata, or tunnelled mode, where decoded video is passed through the vendor layer without returning frame buffers (metadata still applies as described by the interface callbacks).

AV Buffer HAL manages secure and non-secure memory heaps and pools for the media pipeline and related A/V HAL components. AV buffers are referenced by handles as they move across HAL interfaces.

## Video Decoder HAL interfaces

### IVideoDecoderManager (service)

`IVideoDecoderManager` is the service interface used to enumerate and access video decoder resources and operational modes.

It provides:
- `getVideoDecoderIds()` to return the platform list of `IVideoDecoder.Id` values.
- `getSupportedOperationalModes()` to return the operational modes supported by the platform video decoders.
- `getVideoDecoder(videoDecoderId)` to obtain an `IVideoDecoder` interface for a given decoder ID.

### IVideoDecoder (resource instance)

`IVideoDecoder` represents a single video decoder resource instance and provides:
- Capability discovery through `getCapabilities()`.
- Property query through `getProperty(property)` and `getPropertyMulti(properties, out propertyKVList)`.
- State query through `getState()`.
- Session open/close:
  - `open(codec, secure, videoDecoderControllerListener)` returns an `IVideoDecoderController` for feeding buffers and controlling decode flow, and uses the passed `IVideoDecoderControllerListener` for controller callbacks.
  - `close(videoDecoderController)` closes the decoder session when it is in `READY`.
- Event listener registration:
  - `registerEventListener(videoDecoderEventListener)`
  - `unregisterEventListener(videoDecoderEventListener)`

### IVideoDecoderController (resource controller)

`IVideoDecoderController` is used after `IVideoDecoder.open()` succeeds and provides decode flow control and data submission:
- `start()` transitions the decoder from `READY` to `STARTED` (through `STARTING`).
- `stop()` transitions from `STARTED` to `READY` (through `STOPPING`) and frees any input buffers that have been passed for decode but not yet decoded.
- `setProperty(property, propertyValue)` sets controller-related properties.
- `decodeBuffer(nsPresentationTime, bufferHandle)` submits an encoded elementary-stream frame (one frame per call) in an AV buffer handle.
- `flush(reset)` transitions through `FLUSHING`, frees any queued input buffers not decoded, returns pending decoded frames to the frame buffer pool, and optionally resets internal state.
- Stream signalling:
  - `signalDiscontinuity()` indicates PTS discontinuity between buffers.
  - `signalEOS()` signals end-of-stream after the last buffer is submitted and requires an output callback with `FrameMetadata.endOfStream` after all frames are output.
- Codec initialisation data:
  - `parseCodecSpecificData(csdVideoFormat, codecData)` provides codec-specific data required before frame buffers are passed to `decodeBuffer()` when applicable.

### IVideoDecoderControllerListener (controller callbacks)

`IVideoDecoderControllerListener` is a oneway callback interface used by the decoder controller to return output notifications:
- `onFrameOutput(nsPresentationTime, frameBufferHandle, metadata)` is called when a full frame has been decoded and/or when frame metadata needs to be notified.
  - In tunnelled mode, `frameBufferHandle` is `-1` when no frame buffer handle is returned.
  - `metadata` may be null when metadata does not need to be passed, following the rules described in the Video Decoder documentation.
- `onUserDataOutput(nsPresentationTime, userData)` delivers picture user data associated with frames, in presentation order.

### IVideoDecoderEventListener (decoder events)

`IVideoDecoderEventListener` is a oneway callback interface used by `IVideoDecoder` to deliver:
- `onDecodeError(errorCode, vendorErrorCode)` when errors occur.
- `onStateChanged(oldState, newState)` when the decoder transitions to a new state.

## AV Buffer HAL interface (IAVBuffer) as used by the pipeline

### AV buffer service role

`IAVBuffer` provides the central API for buffer management. The AV Buffer HAL manages pools and heaps for secure and non-secure buffers and exposes buffer handles as the exchange mechanism across HAL interfaces.

In the video decode flow, encoded video frames are passed to `IVideoDecoderController.decodeBuffer()` using AV buffer handles. Once the decoder has finished with an input buffer, it is automatically released and returned to the AV Buffer Manager (as described in the controller interface contract). The `IAVBuffer.free(bufferHandle)` API is the public method used to free an AV buffer handle back to its original pool.

### Pools and allocations relevant to video decode

The AV Buffer service supports:
- `createVideoPool(secureHeap, videoDecoderId, listener)` to create a pool targeted for a video decoder resource instance, in either secure or non-secure heap.
- `alloc(poolHandle, size)` to allocate an AV buffer from a pool, returning a globally unique handle.
- `free(bufferHandle)` to release a buffer handle back to its originating pool.
- `destroyPool(poolHandle)` to destroy a pool when empty.

The AV Buffer documentation specifies that handles must be globally unique across all memory pools and heaps, including vendor audio/video frame pools, and that immediate handle reuse after free/destroy is discouraged.

## Operational modes and output behaviour

The Video Decoder documentation defines operational modes (advertised through `IVideoDecoderManager.getSupportedOperationalModes()` and controlled by the `OPERATIONAL_MODE` property):
- `TUNNELLED`: decoded frames are passed directly through the vendor layer; `onFrameOutput()` calls use `frameBufferHandle = -1` when metadata is returned.
- `NON_TUNNELLED`: decoded frames are returned to the client via `onFrameOutput()` with a frame buffer handle; frames are returned in presentation order.
- `GRAPHICS_TEXTURE`: frames are converted to NV12 textures and may run concurrently with tunnelled or non-tunnelled mode if supported.

## Video Decoder states

The Video Decoder documentation states that the Video Decoder HAL follows the standard Session State Management paradigm. The common HAL `State` enum defines the state model used by `IVideoDecoder.getState()` and `IVideoDecoderEventListener.onStateChanged()`:

- `UNKNOWN`
- `CLOSED`
- `OPENING`
- `READY`
- `STARTING`
- `STARTED`
- `FLUSHING`
- `STOPPING`
- `CLOSING`

The documentation also specifies that when a Video Decoder session enters `FLUSHING` or `STOPPING` transitory states it shall free any AV buffers it is holding.

## Typical control and data flow (component interactions)

A typical interaction sequence, as reflected in the Video Decoder documentation sequence diagram, is:
1. Client registers `IVideoDecoderEventListener` on `IVideoDecoder`.
2. Client calls `IVideoDecoder.open(codec, secure, controllerListener)` which transitions the resource from `CLOSED` to `OPENING` to `READY` and returns `IVideoDecoderController`.
3. Client calls `IVideoDecoderController.start()` which transitions from `READY` to `STARTING` to `STARTED`.
4. Client submits encoded frames with `IVideoDecoderController.decodeBuffer(nsPresentationTime, bufferHandle)`.
5. Decoder outputs frames and/or metadata via `IVideoDecoderControllerListener.onFrameOutput(...)` and frees submitted input AV buffers when processing is complete.
6. Client may call `flush(reset)` (state transitions through `FLUSHING`) or `stop()` (state transitions through `STOPPING`) and buffered AV buffers held by the decoder are freed as described.
7. Client calls `IVideoDecoder.close(controller)` (state transitions through `CLOSING` to `CLOSED`).
8. Client unregisters the event listener.
