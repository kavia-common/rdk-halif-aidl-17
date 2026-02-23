<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder_documents/video-decoder-data-flow.md
- Operation: write
- Timestamp: 2026-02-12T04:46:08.193744+00:00
- Restored At: 2026-02-23T05:04:11.251931+00:00
- Task ID: cm219d4578
-->

# Video Decoder Data Flow (HALIF)

## Source links
This document is derived only from the following sources:

- Video Decoder states page: https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/video_decoder/current/video_decoder/#video-decoder-states
- AV Buffer page: https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/av_buffer/current/av_buffer/
- VideoDecoder AIDL definitions: https://github.com/rdkcentral/rdk-halif-aidl/tree/main/videodecoder/current/com/rdk/hal/videodecoder

## Actors and interfaces involved in data flow
The core interfaces involved in passing coded video into the decoder and receiving decoded output/metadata are:

### AV Buffer HAL
The AV Buffer service provides buffer pools and handle-based allocation/free, covering secure and non-secure memory types:

- `com.rdk.hal.avbuffer.IAVBuffer`
  - `createVideoPool(boolean secureHeap, IVideoDecoder.Id videoDecoderId, IAVBufferSpaceListener listener) -> Pool`
  - `alloc(Pool poolHandle, int size) -> long` (returns an AV buffer handle)
  - `free(long bufferHandle) -> boolean` (returns any handle to its original pool)
- Buffers and pools are referenced by handles, and handles are intended to be unique across the system.

### Video Decoder HAL
The Video Decoder HAL is split into a resource interface and a controller interface:

- `com.rdk.hal.videodecoder.IVideoDecoderManager`
  - `getVideoDecoderIds() -> IVideoDecoder.Id[]`
  - `getSupportedOperationalModes() -> OperationalMode[]`
  - `getVideoDecoder(IVideoDecoder.Id) -> @nullable IVideoDecoder`
- `com.rdk.hal.videodecoder.IVideoDecoder`
  - `open(Codec codec, boolean secure, IVideoDecoderControllerListener listener) -> @nullable IVideoDecoderController`
  - `close(IVideoDecoderController controller) -> boolean`
  - `registerEventListener(IVideoDecoderEventListener listener) -> boolean`
  - `getState() -> com.rdk.hal.State`
- `com.rdk.hal.videodecoder.IVideoDecoderController`
  - `start()`
  - `decodeBuffer(long nsPresentationTime, long bufferHandle) -> boolean`
  - `flush(boolean reset)`
  - `stop()`
  - `signalDiscontinuity()`
  - `signalEOS()`
  - `parseCodecSpecificData(CSDVideoFormat csdVideoFormat, byte[] codecData) -> boolean`
- `com.rdk.hal.videodecoder.IVideoDecoderControllerListener` (callbacks)
  - `onFrameOutput(long nsPresentationTime, long frameBufferHandle, @nullable FrameMetadata metadata)`
  - `onUserDataOutput(long nsPresentationTime, byte[] userData)`
- `com.rdk.hal.videodecoder.IVideoDecoderEventListener` (state/error callbacks)
  - `onStateChanged(State oldState, State newState)`
  - `onDecodeError(ErrorCode errorCode, int vendorErrorCode)`

## Data flow overview
The data flow is handle-based. Encoded video frames are placed into AV buffers allocated from the AV Buffer service, then submitted to the video decoder via `IVideoDecoderController.decodeBuffer()`. Buffer ownership is transferred to the decoder, which frees the input AV buffers after processing them.

Decoded output is delivered either:
- In non-tunnelled mode: via `IVideoDecoderControllerListener.onFrameOutput()` with a decoded frame buffer handle plus optional `FrameMetadata`.
- In tunnelled mode: frames are not delivered back as buffers; callbacks use `frameBufferHandle = -1` when a callback is made to deliver metadata (per the Video Decoder HAL documentation).

## Step-by-step data flow (client perspective)

### 1) Get the AV Buffer service and create pools
From the AV Buffer documentation:

A client creates a video pool using `IAVBuffer.createVideoPool()`. The pool can be secure or non-secure (`secureHeap` boolean). Pools are used to allocate per-frame AV buffers with `IAVBuffer.alloc()`.

This pool is associated with a specific video decoder resource ID (`IVideoDecoder.Id`) so that the vendor can size and manage the pool appropriately.

### 2) Discover a video decoder resource and supported operational modes
From `IVideoDecoderManager`:

- The client obtains available decoder IDs with `getVideoDecoderIds()`.
- The client queries supported operational modes with `getSupportedOperationalModes()`.
- The client obtains an `IVideoDecoder` instance for a selected ID with `getVideoDecoder()`.

Operational modes are represented by `OperationalMode`:
- `TUNNELLED`
- `NON_TUNNELLED`
- `GRAPHICS_TEXTURE`

### 3) Register event listener (state/error)
The client may register `IVideoDecoderEventListener` via `IVideoDecoder.registerEventListener()` to receive:
- `onStateChanged(oldState, newState)`
- `onDecodeError(errorCode, vendorErrorCode)`

### 4) Open the decoder and obtain the controller
The client calls:

`IVideoDecoder.open(codec, secure, IVideoDecoderControllerListener listener) -> IVideoDecoderController`

From the Video Decoder HAL documentation and AIDL comments:
- On success, the decoder transitions `CLOSED -> OPENING -> READY`, notified via `IVideoDecoderEventListener.onStateChanged()`.
- The `IVideoDecoderControllerListener` passed into `open()` is the callback channel for decoded output and metadata.

### 5) Start decoding
The client calls `IVideoDecoderController.start()`.

From the Video Decoder HAL documentation and AIDL comments:
- State transitions for start are `READY -> STARTING -> STARTED`, notified via `onStateChanged()`.

### 6) Allocate input AV buffers and submit to the decoder
From AV Buffer and Video Decoder AIDL:

1. The client allocates an AV buffer for a single coded frame:
   - `long bufferHandle = IAVBuffer.alloc(videoPool, size)`

2. The coded frame is placed into the allocated buffer (AV Buffer documentation describes mapping/unmapping for non-secure buffers via the helper library, and that secure memory cannot be mapped in unprivileged processes).

3. The client submits the coded frame to the decoder:
   - `IVideoDecoderController.decodeBuffer(nsPresentationTime, bufferHandle) -> boolean`

The `nsPresentationTime` is in nanoseconds (as described in the Video Decoder HAL documentation).

Ownership and freeing of input buffers:
- `IVideoDecoderController.decodeBuffer()` explicitly states that once the decoder has finished processing the buffer, it is automatically released and returned to the AV Buffer Manager, and the caller must not modify or free the buffer after submission.
- The Video Decoder states section further emphasizes that when entering transitory states like `FLUSHING` or `STOPPING`, the decoder frees any AV buffers it is holding.

### 7) Receive decoded output and metadata
The primary output callback is:

`IVideoDecoderControllerListener.onFrameOutput(nsPresentationTime, frameBufferHandle, metadata)`

From the Video Decoder HAL documentation:

- Frame metadata (`FrameMetadata`) is associated with decoded frames and is delivered via `onFrameOutput()`.
- To conserve CPU, metadata is only included:
  - with the first decoded frame after `start()`,
  - with the first decoded frame after `flush()`,
  - or when metadata changes.
- If metadata does not need to be passed, `metadata` should be `null`.

Non-tunnelled vs tunnelled handle behavior (from Video Decoder operational modes section):
- In `NON_TUNNELLED`, decoded frames are received back in frame buffers over `onFrameOutput()`.
- In `TUNNELLED`, decoded frames are not received back in frame buffers, and calls to `onFrameOutput()` shall set `frameBufferHandle = -1` to indicate no decoded frame buffer handle is passed back (while `FrameMetadata` must still be returned in the usual way when needed).

User data output:
- The decoder may also output per-frame user data via `onUserDataOutput(nsPresentationTime, userData)`, ordered in the same frame presentation order as output frames.

### 8) Discontinuities and end-of-stream
From `IVideoDecoderController`:

- `signalDiscontinuity()` is called between buffers when the client knows of PTS discontinuities. The Video Decoder HAL documentation states that for the first input AV buffer after the discontinuity, the next output `FrameMetadata` should indicate the discontinuity.
- `signalEOS()` is called after the last AV buffer is passed for decode. The Video Decoder HAL documentation states that after all frames have been output, the decoder must emit `FrameMetadata` with `endOfStream=true`.

### 9) Flush and stop behavior (buffer freeing)
From the Video Decoder states documentation and `IVideoDecoderController`:

- `flush(reset)` transitions the decoder into a `FLUSHING` state (transitory), frees any input AV buffers that have been submitted but not decoded yet, then returns to `STARTED`.
- `stop()` transitions the decoder into a `STOPPING` state (transitory) and frees any input AV buffers that have been submitted but not decoded, then returns to `READY`.

The Video Decoder states section explicitly states:
- When a Video Decoder session enters `FLUSHING` or `STOPPING` transitory state it shall free any AV buffers it is holding.

### 10) Close
The client calls `IVideoDecoder.close(controller)`.

From the Video Decoder states documentation and AIDL comments:
- `close()` transitions `READY -> CLOSING -> CLOSED`, notified by `onStateChanged()`.

## Video decoder states data-flow sequence (from the states section)
The Video Decoder HAL documentation provides an example sequence showing how input buffers are submitted and freed, and how output is returned:

- Register `IVideoDecoderEventListener`
- `open()` transitions `CLOSED -> OPENING -> READY`
- `start()` transitions `READY -> STARTING -> STARTED`
- Client calls `decodeBuffer(pts, bufferHandle=1)`, `decodeBuffer(pts, bufferHandle=2)`
- Decoder calls `onFrameOutput(pts, frameBufferHandle=..., metadata)`
- Decoder frees input buffer handle(s) via `IAVBuffer.free(bufferHandle=...)` after processing
- `flush()` transitions `STARTED -> FLUSHING -> STARTED` and frees any held buffers
- `stop()` transitions `STARTED -> STOPPING -> READY` and frees any held buffers
- `close()` transitions `READY -> CLOSING -> CLOSED`
- Unregister listener

## Handle ownership summary (input vs output)
From the AV Buffer and Video Decoder HAL documentation:

- Input coded frame buffers are AV Buffer handles allocated from a pool (`IAVBuffer.alloc()`), submitted to `decodeBuffer()`, and then freed by the decoder after processing.
- Output decoded frame buffers are delivered as handles in `onFrameOutput()` only in non-tunnelled mode. In tunnelled mode, `frameBufferHandle` is `-1` when `onFrameOutput()` is used for metadata delivery.
- Buffer freeing is centralized through `IAVBuffer.free(handle)`, and the video decoder is required to free any AV buffers it holds when entering `FLUSHING` or `STOPPING`.
