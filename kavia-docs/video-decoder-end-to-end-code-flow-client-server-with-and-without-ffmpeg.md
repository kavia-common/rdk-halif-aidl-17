<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder_documents/video-decoder-end-to-end-code-flow-client-server-with-and-without-ffmpeg.md
- Operation: write
- Timestamp: 2026-02-12T05:03:45.178228+00:00
- Restored At: 2026-02-23T05:04:11.252181+00:00
- Task ID: cm219d4578
-->

# Video Decoder AIDL HAL End-to-End Code Flow (Client and Server) — With and Without FFmpeg

## Scope and sources

This document describes the end-to-end code flow for the Video Decoder AIDL HAL from both client and server perspectives. It covers data flow, control flow, and sequence flow, and enumerates the APIs in and out of the Video Decoder component. Two variants are included: a vendor decode path without FFmpeg and an FFmpeg-assisted path for codec decoding purposes (for example H.264), while keeping the external AIDL APIs unchanged.

This document is based only on the following provided links (represented here by their local repository equivalents and AIDL definitions):

- Video Decoder HAL documentation (states and behavior): `rdk-halif-aidl-17/docs/halif/video_decoder/current/video_decoder.md`  
  (Provided link: https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/video_decoder/current/video_decoder/#video-decoder-states)
- AV Buffer HAL documentation: `rdk-halif-aidl-17/docs/halif/av_buffer/current/av_buffer.md`  
  (Provided link: https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/av_buffer/current/av_buffer/)
- Video Decoder AIDL interface definitions: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/*`  
  (Provided link: https://github.com/rdkcentral/rdk-halif-aidl/tree/main/videodecoder/current/com/rdk/hal/videodecoder)

Additionally, the local vComponent stub service in this workspace is used only to ground the “server perspective” (service registration and control-plane behavior) without adding non-linked behavior:
- `10002556%2FTEVDevice/src/service/vcomponent_VideoDecoderService.cpp`
- `10002556%2FTEVDevice/src/aidl/vcomponent_VideoDecoder.cpp`
- `10002556%2FTEVDevice/src/controller/vcomponent_VideoDecoderController.cpp`
- `10002556%2FTEVDevice/include/videodecoder/vcomponent_VideoDecoderController.h`

## Components and interfaces (what “client” and “server” mean here)

The Video Decoder HAL uses AIDL/Binder interfaces where the “client” is any process that obtains the service and calls the HAL APIs, and the “server” is the vendor-layer service implementation that registers Binder services and processes those calls.

### Video Decoder AIDL interfaces (in/out)

The AIDL interface set for Video Decoder is:

- `IVideoDecoderManager` (Service)
- `IVideoDecoder` (Instance)
- `IVideoDecoderController` (Instance, returned by `IVideoDecoder.open`)
- `IVideoDecoderEventListener` (Client callback, registered on `IVideoDecoder`)
- `IVideoDecoderControllerListener` (Client callback, passed into `IVideoDecoder.open`)

### AV Buffer AIDL interfaces (in/out)

The Video Decoder control/data flow relies on buffer handles managed by AV Buffer:

- `IAVBuffer` (Service)
- `IAVBufferSpaceListener` (Client callback, passed into pool creation)

## API inventory (all APIs in and out of the Video Decoder component)

This section enumerates the AIDL APIs that form the control plane and data plane around Video Decoder and AV Buffer, as they are the API surface involved in the end-to-end flow.

### IVideoDecoderManager (client calls into server)

From `IVideoDecoderManager.aidl`:

- `IVideoDecoder.Id[] getVideoDecoderIds()`
- `OperationalMode[] getSupportedOperationalModes()`
- `@nullable IVideoDecoder getVideoDecoder(in IVideoDecoder.Id videoDecoderId)`

### IVideoDecoder (client calls into server)

From `IVideoDecoder.aidl`:

- `Capabilities getCapabilities()`
- `@nullable PropertyValue getProperty(in Property property)`
- `boolean getPropertyMulti(in Property[] properties, out PropertyKVPair[] propertyKVList)`
- `State getState()`
- `@nullable IVideoDecoderController open(in Codec codec, in boolean secure, in IVideoDecoderControllerListener videoDecoderControllerListener)`
- `boolean close(in IVideoDecoderController videoDecoderController)`
- `boolean registerEventListener(in IVideoDecoderEventListener videoDecoderEventListener)`
- `boolean unregisterEventListener(in IVideoDecoderEventListener videoDecoderEventListener)`

### IVideoDecoderController (client calls into server)

From `IVideoDecoderController.aidl`:

- `void start()`
- `void stop()`
- `boolean setProperty(in Property property, in PropertyValue propertyValue)`
- `boolean decodeBuffer(in long nsPresentationTime, in long bufferHandle)`
- `void flush(in boolean reset)`
- `void signalDiscontinuity()`
- `void signalEOS()`
- `boolean parseCodecSpecificData(in CSDVideoFormat csdVideoFormat, in byte[] codecData)`

### IVideoDecoderEventListener (server calls back into client)

From `IVideoDecoderEventListener.aidl` (oneway callbacks):

- `void onDecodeError(in ErrorCode errorCode, in int vendorErrorCode)`
- `void onStateChanged(in State oldState, in State newState)`

### IVideoDecoderControllerListener (server calls back into client)

From `IVideoDecoderControllerListener.aidl` (oneway callbacks):

- `void onFrameOutput(in long nsPresentationTime, in long frameBufferHandle, in @nullable FrameMetadata metadata)`
- `void onUserDataOutput(in long nsPresentationTime, in byte[] userData)`

### IAVBuffer (client calls into server; Video Decoder also frees input handles)

From `IAVBuffer.aidl`:

- `HeapMetrics getHeapMetrics(in boolean secureHeap)`
- `Pool createVideoPool(in boolean secureHeap, in IVideoDecoder.Id videoDecoderId, in IAVBufferSpaceListener listener)`
- `Pool createAudioPool(in boolean secureHeap, in IAudioDecoder.Id audioDecoderId, in IAVBufferSpaceListener listener)` (listed for completeness, but not part of video decode flow)
- `boolean destroyPool(in Pool poolHandle)`
- `PoolMetrics getPoolMetrics(in Pool poolHandle)`
- `PoolMetrics[] getAllPoolMetrics(in boolean secureHeap)`
- `long alloc(in Pool poolHandle, in int size)`
- `boolean notifyWhenSpaceAvailable(in Pool poolHandle, in int size)`
- `boolean trimSize(in long bufferHandle, in int newSize)`
- `boolean free(in long bufferHandle)`
- `boolean isValid(in long bufferHandle)`
- `long[] getAllocList(in Pool poolHandle)`
- `byte[] calculateSHA1(in long bufferHandle)` (optional debug-only behavior)

### IAVBufferSpaceListener (server calls back into client)

From `IAVBufferSpaceListener.aidl` (oneway callback):

- `void onSpaceAvailable()`

## State model and required sequencing (control flow constraints)

The Video Decoder HAL follows standard Session State Management. The key state transitions described in `video_decoder.md` and in AIDL comments are:

- `open()` is only valid when the decoder is `CLOSED`. On success the server transitions `CLOSED -> OPENING -> READY` and notifies via `IVideoDecoderEventListener.onStateChanged`.
- `start()` is only valid when the decoder is `READY`. On success the server transitions `READY -> STARTING -> STARTED` and notifies via `onStateChanged`.
- While `STARTED`, the client can submit coded frame buffers via `decodeBuffer(nsPresentationTime, bufferHandle)` and optionally:
  - send codec specific data before frames using `parseCodecSpecificData(...)` (requires `STARTED`)
  - flush pipeline using `flush(reset)`
  - declare discontinuities using `signalDiscontinuity()`
  - end stream using `signalEOS()`
- `stop()` is only valid when the decoder is `STARTED`. On success the server transitions `STARTED -> STOPPING -> READY` and frees any queued input buffers that have not yet been decoded.
- `close()` is only valid when the decoder is `READY`. On success the server transitions `READY -> CLOSING -> CLOSED` and releases controller resources.

A key buffer ownership rule described in `IVideoDecoderController.decodeBuffer()` is that once a client submits `bufferHandle`, the decoder takes ownership and must later release it back to AV Buffer, typically by calling `IAVBuffer.free(bufferHandle)` when it has finished processing it.

## End-to-end flow from the client perspective

The client perspective includes: discovering resources, setting up AVBuffer pools, opening a decoder session, starting, pushing coded frames, receiving output callbacks, and closing.

### 1) Discover Video Decoder resources and modes (control plane)

1. The client obtains `IVideoDecoderManager` from the service manager (mechanism described in `video_decoder.md` under initialization).
2. The client calls `getVideoDecoderIds()` to enumerate available decoder IDs.
3. The client calls `getSupportedOperationalModes()` to understand the operational mode support.
4. The client calls `getVideoDecoder(videoDecoderId)` to get a specific `IVideoDecoder` instance.

In this phase, the client may also call `IVideoDecoder.getCapabilities()` and `getProperty` / `getPropertyMulti` to inspect supported codecs and properties.

### 2) Create AVBuffer video pools and allocate coded-frame buffers (data plane setup)

Based on `av_buffer.md` and `IAVBuffer.aidl`, the client does the following:

1. The client obtains the `IAVBuffer` service.
2. The client creates one or more video pools using:
   - `createVideoPool(secureHeap=false, videoDecoderId, listener)` for non-secure buffers, and/or
   - `createVideoPool(secureHeap=true, videoDecoderId, listener)` for secure buffers if secure playback is needed.
3. For each coded video frame, the client allocates a buffer handle from the pool with:
   - `alloc(poolHandle, sizeBytes)` producing `bufferHandle`.
4. The client writes frame bytes into the handle (the `av_buffer.md` describes a helper library for mapping/unmapping non-secure buffers, but the helper library API itself is not part of the AIDL list and is not enumerated here).
5. The client now holds a `bufferHandle` representing a coded (compressed) video frame.

### 3) Register for decoder session events (server -> client callbacks)

The client registers an `IVideoDecoderEventListener`:

- `registerEventListener(videoDecoderEventListener)`

This enables:
- `onStateChanged(oldState, newState)`
- `onDecodeError(errorCode, vendorErrorCode)`

### 4) Open session and get controller (control plane; session creation)

The client opens the decoder with:

- `controller = IVideoDecoder.open(codec, secure, videoDecoderControllerListener)`

The open call also passes an `IVideoDecoderControllerListener` that will be used for output callbacks.

On success, the client receives:
- a non-null `IVideoDecoderController`

And will observe state callbacks:
- `onStateChanged(CLOSED, OPENING)`
- `onStateChanged(OPENING, READY)`

### 5) Start decoding (control plane)

The client calls:

- `controller.start()`

On success, the client observes:
- `onStateChanged(READY, STARTING)`
- `onStateChanged(STARTING, STARTED)`

### 6) Optional: send codec-specific data before frames (control plane)

If needed by stream/container, while in `STARTED` the client calls:

- `controller.parseCodecSpecificData(csdVideoFormat, codecDataBytes)`

The AIDL describes formats such as:
- AVC decoder configuration record (H.264)
- HEVC decoder configuration record (H.265)
- AV1 decoder configuration record

### 7) Submit coded frames for decode (data plane) and receive outputs

For each coded frame:

1. The client calls:
   - `ok = controller.decodeBuffer(nsPresentationTime, bufferHandle)`

2. If `ok == true`, ownership of `bufferHandle` transfers to the decoder. The client must not modify or free the buffer.

3. Output path depends on operational mode (from `video_decoder.md`):

   - **Non-tunnelled mode**:  
     The server sends decoded output via:
     - `IVideoDecoderControllerListener.onFrameOutput(nsPresentationTime, frameBufferHandle, metadataOrNull)`  
       where `frameBufferHandle` is a handle to a decoded 2D frame buffer.  
     The client later passes `frameBufferHandle` downstream (the document states it is passed to Video Sink for queuing before presentation and then freed).

   - **Tunnelled mode**:  
     The server does not return decoded frame buffers to the client. When metadata must be returned, `onFrameOutput` is called with:
     - `frameBufferHandle = -1`  
     When there is no metadata update, the spec states that no `onFrameOutput()` should be made in tunnelled-only operation.

4. Independently, picture user data may be delivered by:
   - `IVideoDecoderControllerListener.onUserDataOutput(nsPresentationTime, userDataBytes)`

5. After the server has finished processing an input coded buffer, it frees that buffer back to AV Buffer using:
   - `IAVBuffer.free(bufferHandle)`

If the client needs to handle “decode buffer full” behavior, it observes:
- `decodeBuffer(...)` returns `false` when the decode buffer is full.

### 8) Handle stream events: discontinuity, flush, end-of-stream (control flow)

- **Discontinuity**:  
  The client calls `controller.signalDiscontinuity()` between frames; the next output metadata indicates the discontinuity.

- **Flush**:  
  The client calls `controller.flush(reset)` while `STARTED`. The service transitions `STARTED -> FLUSHING -> STARTED`, frees queued input buffers, returns pending decoded frames to the pool, and optionally resets internal state.

- **End of stream**:  
  The client calls `controller.signalEOS()` after the last `decodeBuffer`. The service continues processing remaining frames and must eventually deliver:
  - `FrameMetadata.endOfStream = true` in an `onFrameOutput` callback after all frames have been output.

### 9) Stop and close session; cleanup pools (control plane and AVBuffer cleanup)

1. The client stops decode:
   - `controller.stop()`
   State transitions: `STARTED -> STOPPING -> READY`, and the decoder frees outstanding coded input buffers it holds.

2. The client closes the session:
   - `IVideoDecoder.close(controller)`

3. The client unregisters the event listener:
   - `unregisterEventListener(listener)`

4. The client destroys AVBuffer pools after all allocations are freed:
   - `destroyPool(poolHandle)`

## End-to-end flow from the server perspective (vendor service)

This section explains the “server” side responsibilities described in the HAL docs and shown in the local stub service code (service registration and a control-plane YAML hook). The AIDL links define the external behavior; the server must implement those semantics.

### Service initialization and registration (what the server must do)

From `video_decoder.md` initialization section:

- The vendor layer starts a system service (systemd unit referenced in the docs) and registers the AIDL interface with the service manager using:
  - `IVideoDecoderManager.serviceName` for the manager service.

The local stub code demonstrates binder registration for `IVideoDecoder` directly (as a stub). In `10002556%2FTEVDevice/src/service/vcomponent_VideoDecoderService.cpp` the program:

1. Creates a decoder stub object.
2. Registers binder service under:
   - `AidlBnVideoDecoder::descriptor + "/default"`
3. Starts binder thread pool and joins.

Although this stub registers `IVideoDecoder`, the HAL documentation focuses on `IVideoDecoderManager` as the main entry point service for resource management. The server perspective for the HAL therefore includes implementing a manager service that creates/returns instances, and per-instance objects that enforce session ownership and state transitions.

### Server-side control plane: state machine enforcement

The server must implement the state transition rules implied by the AIDL documentation:

- `open()` only in `CLOSED`; transitions to `OPENING` then `READY`.
- `start()` only in `READY`; transitions to `STARTING` then `STARTED`.
- `stop()` only in `STARTED`; transitions to `STOPPING` then `READY` and frees queued buffers.
- `flush(reset)` only in `STARTED`; transitions to `FLUSHING` then `STARTED`, frees queued buffers, optionally resets internal state.
- `close()` only in `READY`; transitions to `CLOSING` then `CLOSED`.

The server must emit state transitions to all registered `IVideoDecoderEventListener` instances using `onStateChanged(oldState, newState)`.

### Server-side data plane: buffer ownership and freeing

From `IVideoDecoderController.decodeBuffer()`:

- On successful acceptance of an input coded buffer handle, the server takes ownership of the handle.
- Once processing is complete, the server releases the coded buffer by calling `IAVBuffer.free(bufferHandle)` (as shown in the `video_decoder.md` sequence diagram).

### Server-side output callbacks

The server delivers output using `IVideoDecoderControllerListener`:

- `onFrameOutput(nsPresentationTime, frameBufferHandle, metadataOrNull)`
- `onUserDataOutput(nsPresentationTime, userDataBytes)`

The exact behavior depends on operational mode:

- In **non-tunnelled mode**, `frameBufferHandle` carries a valid decoded frame buffer handle.
- In **tunnelled mode**, decoded video is passed internally through the vendor layer, and `frameBufferHandle` must be `-1` when `onFrameOutput` is used for metadata-only updates.

### Client crash handling

From the comment in `IVideoDecoder.open()`:

- If the client that opened the `IVideoDecoderController` crashes, then the controller has `stop()` and `close()` implicitly called to perform clean up.

This is a server responsibility to ensure the session is cleaned up automatically on binder death.

### Server-side vComponent control-plane YAML (as present in this workspace)

The local stub includes optional WebSocket-driven YAML updates:

- `vcomponent_VideoDecoderService.cpp` connects (optionally) to a WebSocket URL from `VIDEODECODER_WS_URL`
- On messages, it calls `VideoDecoderController.applyYamlUpdate(payload)`
- `VideoDecoderController.loadInitialProfile()` reads the HFP YAML path from `VIDEODECODER_HFP_PATH`

This YAML mechanism is orthogonal to the AIDL decode pipeline but represents a server-side configuration path in the local code.

## Sequence flows (with explicit API calls)

This section provides concise sequence flows using only the provided API surfaces and the behavior described in the linked docs.

### Sequence A: Basic non-tunnelled decode (no FFmpeg)

This variant assumes the vendor implementation decodes in hardware/vendor codec implementation directly and outputs decoded frames back to the client (non-tunnelled).

1. Client obtains `IVideoDecoderManager`.
2. Client calls `getVideoDecoderIds()`.
3. Client calls `getVideoDecoder(id)` to obtain `IVideoDecoder`.
4. Client obtains `IAVBuffer`.
5. Client calls `createVideoPool(secureHeap=?, videoDecoderId, listener)` to create pool(s).
6. Client registers `IVideoDecoderEventListener` using `registerEventListener`.
7. Client calls `open(codec, secure, controllerListener)` and receives `IVideoDecoderController`.
8. Server emits `onStateChanged(CLOSED, OPENING)` then `onStateChanged(OPENING, READY)`.
9. Client calls `controller.start()`.
10. Server emits `onStateChanged(READY, STARTING)` then `onStateChanged(STARTING, STARTED)`.
11. For each coded frame:
    - Client calls `IAVBuffer.alloc(pool, size)` to get `bufferHandle`.
    - Client loads frame bytes into `bufferHandle` (mapping/copy described in AVBuffer docs).
    - Client calls `controller.decodeBuffer(ptsNs, bufferHandle)`.
    - Server decodes frame and calls `controllerListener.onFrameOutput(ptsNs, frameBufferHandle, metadataOrNull)`.
    - Server calls `IAVBuffer.free(bufferHandle)` after processing coded input.
12. Client stops:
    - `controller.stop()`, server emits `STARTED -> STOPPING -> READY` and frees queued inputs.
13. Client closes:
    - `IVideoDecoder.close(controller)`, server emits `READY -> CLOSING -> CLOSED`.
14. Client destroys pools:
    - `destroyPool(poolHandle)` when all allocations are freed.

### Sequence B: Tunnelled decode (no FFmpeg)

This variant assumes tunnelled operational mode where decoded video does not come back to the client as frame buffers.

The sequence is the same as Sequence A, except for output:

- Server does not deliver decoded frame handles.
- When metadata needs updating, server calls:
  - `onFrameOutput(ptsNs, -1, metadataOrNull)`
- When there is no metadata and tunnelled-only mode is active, the docs state the server should not call `onFrameOutput()`.

### Sequence C: Decode with FFmpeg-assisted codec processing (external API unchanged)

This variant describes the flow where the external AIDL APIs remain exactly the same, but the server’s internal decode engine uses FFmpeg for codec decode (for example H.264). The purpose here is to show “with FFmpeg” vs “without FFmpeg” as an implementation choice behind the same `decodeBuffer()` API.

From the client’s perspective, **nothing changes** because the AIDL API surface is unchanged. The same sequences apply:

- Client still calls `open(codec, ...)`, `start()`, optionally `parseCodecSpecificData(...)`, then `decodeBuffer(ptsNs, bufferHandle)`.
- Output callbacks remain `onFrameOutput(...)` and/or `onUserDataOutput(...)`.

From the server’s perspective, the internal steps after `decodeBuffer()` differ:

1. Server receives `decodeBuffer(ptsNs, bufferHandle)`.
2. Server reads coded bytes from the AV buffer referenced by `bufferHandle` using vendor AVBuffer mechanisms described in `av_buffer.md` (mapping/copy; secure vs non-secure constraints).
3. Server performs codec decode using FFmpeg internally (example: H.264).
4. Server produces decoded output in one of the supported operational modes:
   - Non-tunnelled: creates/obtains a decoded frame buffer handle and calls `onFrameOutput(ptsNs, frameBufferHandle, metadataOrNull)`.
   - Tunnelled: passes decoded output internally and only calls `onFrameOutput(ptsNs, -1, metadataOrNull)` when metadata needs to be delivered.
5. Server frees the coded input `bufferHandle` by calling `IAVBuffer.free(bufferHandle)` after processing is complete.

Because the provided links do not define FFmpeg-specific APIs or integration points, the only FFmpeg-related difference that can be stated here (without adding extra information) is that FFmpeg is an internal codec implementation detail behind the same `decodeBuffer()` data flow and callback semantics.

## Data flow summary (handles and ownership)

The essential data items that move across APIs are:

- `IVideoDecoder.Id` values: flow from server to client via `getVideoDecoderIds()`.
- AVBuffer `Pool` handles: flow from AVBuffer service to client via `createVideoPool(...)`.
- Coded input buffer handles (`long bufferHandle`): allocated by client via `IAVBuffer.alloc(...)`, passed to Video Decoder via `decodeBuffer(...)`, and freed by the Video Decoder server via `IAVBuffer.free(...)`.
- Decoded frame buffer handles (`long frameBufferHandle`): produced/managed by the vendor decode implementation and returned to the client via `onFrameOutput(...)` only in non-tunnelled mode. The docs state these are later passed to Video Sink and then freed.
- Metadata objects: flow from server to client via `onFrameOutput(..., metadata)` and may be `null` to indicate no metadata change.

## Control flow summary (session lifecycle)

The control plane is dominated by:

- session creation and selection: `IVideoDecoderManager.getVideoDecoderIds()`, `getVideoDecoder()`, and `IVideoDecoder.open()`
- session lifecycle: `start()`, `stop()`, `flush(reset)`, `close()`
- stream signalling: `signalDiscontinuity()`, `signalEOS()`
- notifications:
  - state + error: `IVideoDecoderEventListener`
  - output frames and user data: `IVideoDecoderControllerListener`

## Minimal mermaid sequence (canonical example from linked docs)

The linked Video Decoder documentation includes a representative sequence flow. The key API calls shown there are consistent with:

- `registerEventListener`
- `open`
- `start`
- `decodeBuffer`
- output callback `onFrameOutput`
- freeing inputs via `IAVBuffer.free`
- `flush`
- `stop`
- `close`
- `unregisterEventListener`

This is the canonical reference sequence described by the provided link, and the variants above specialize it by operational mode and by internal decode engine (FFmpeg vs non-FFmpeg).

