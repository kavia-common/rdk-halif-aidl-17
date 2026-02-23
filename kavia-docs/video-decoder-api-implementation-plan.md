<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder_documents/video-decoder-api-implementation-plan.md
- Operation: write
- Timestamp: 2026-02-12T04:59:56.885901+00:00
- Restored At: 2026-02-23T05:04:11.252094+00:00
- Task ID: cm219d4578
-->

# Video Decoder API-wise Implementation Plan (In/Out)

## Scope and allowed sources

This plan is limited strictly to the following sources:

- Video Decoder HALIF documentation: `rdk-halif-aidl-17/docs/halif/video_decoder/current/video_decoder.md`
- AV Buffer HALIF documentation: `rdk-halif-aidl-17/docs/halif/av_buffer/current/av_buffer.md`
- AIDL interface definitions in: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/`
- AV Buffer AIDL definitions in: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/`

The plan focuses on API-wise inputs and outputs of the Video Decoder component, including binder call sequencing, buffer handle ownership, callback behavior, and state transitions.

## Component boundary and APIs

### Server-side services and objects to implement

The vendor layer must provide binder services implementing these AIDL interfaces:

1. `com.rdk.hal.videodecoder.IVideoDecoderManager` (service instance name must be `IVideoDecoderManager.serviceName`, value `"VideoDecoderManager"`)
2. `com.rdk.hal.videodecoder.IVideoDecoder` (one instance per decoder resource ID)
3. `com.rdk.hal.videodecoder.IVideoDecoderController` (created/returned by `IVideoDecoder.open()`; one per open session)

The Video Decoder also interacts with the AV Buffer HAL:

- `com.rdk.hal.avbuffer.IAVBuffer` (service instance name must be `IAVBuffer.serviceName`, value `"AVBuffer"`)

### Client-provided callback interfaces (inputs into the server)

The client provides binder objects to the server via `open()` and `registerEventListener()`:

- `com.rdk.hal.videodecoder.IVideoDecoderEventListener` (registered on `IVideoDecoder`)
- `com.rdk.hal.videodecoder.IVideoDecoderControllerListener` (passed into `IVideoDecoder.open()`)

The server must call back to the client on these interfaces:

- `IVideoDecoderEventListener.onStateChanged(oldState, newState)`
- `IVideoDecoderEventListener.onDecodeError(errorCode, vendorErrorCode)`
- `IVideoDecoderControllerListener.onFrameOutput(nsPresentationTime, frameBufferHandle, metadata)`
- `IVideoDecoderControllerListener.onUserDataOutput(nsPresentationTime, userData)`

## Global state model (Session State Management)

The Video Decoder uses `com.rdk.hal.State`:

- `UNKNOWN`, `CLOSED`, `OPENING`, `READY`, `STARTING`, `STARTED`, `FLUSHING`, `STOPPING`, `CLOSING`

### State transition requirements

#### Open / close path (IVideoDecoder)

- Precondition: `IVideoDecoder.open()` requires `State::CLOSED`.
- On `open(codec, secure, controllerListener)` success:
  - Transition `CLOSED -> OPENING -> READY`.
  - Notify all registered `IVideoDecoderEventListener`:
    - `onStateChanged(CLOSED, OPENING)`
    - `onStateChanged(OPENING, READY)`
  - Return an `IVideoDecoderController` binder object tied to the open session.

- Precondition: `IVideoDecoder.close(controller)` requires `State::READY`.
- On `close(controller)` success:
  - Transition `READY -> CLOSING -> CLOSED`.
  - Notify listeners:
    - `onStateChanged(READY, CLOSING)`
    - `onStateChanged(CLOSING, CLOSED)`
  - Destroy/release the controller session.

#### Start / stop path (IVideoDecoderController)

- Precondition: `IVideoDecoderController.start()` requires `State::READY`.
- On success: transition `READY -> STARTING -> STARTED` and notify via `IVideoDecoderEventListener.onStateChanged()` for both steps.

- Precondition: `IVideoDecoderController.stop()` requires `State::STARTED`.
- On success:
  - Transition `STARTED -> STOPPING -> READY` and notify via `onStateChanged()` for both steps.
  - Must free any input AV buffers that were submitted via `decodeBuffer()` but not yet decoded. The docs specify this is effectively the same as a flush.

#### Flush path (IVideoDecoderController)

- Precondition: `IVideoDecoderController.flush(reset)` requires `State::STARTED`.
- On flush:
  - Transition `STARTED -> FLUSHING -> STARTED` and notify via `onStateChanged()` for both steps.
  - Must free any input AV buffers that were submitted but not yet decoded.
  - Must return any pending decoded frames due for callback back to the (vendor-managed) decoded frame buffer pool.
  - If `reset=true`, internal state is fully reset back to its opened `READY` state (as stated in the `flush()` AIDL comment). API-wise, state transitions still include FLUSHING and return to STARTED per the documentation sequence example. The internal decode pipeline must be re-primed such that subsequent output metadata follows “first frame after flush” rules.

### Illegal state enforcement

The AIDL comments define these preconditions, which should be enforced:

- `open()` only in `CLOSED` (otherwise `EX_ILLEGAL_STATE`)
- `close()` only in `READY`
- `start()` only in `READY`
- `stop()` only in `STARTED`
- `flush()` only in `STARTED`
- `decodeBuffer()` only in `STARTED`
- `signalDiscontinuity()` only in `STARTED`
- `signalEOS()` only in `STARTED`
- `parseCodecSpecificData()` only in `STARTED`

The implementation plan assumes these exceptions/states are part of the API contract and must be applied consistently.

## API-wise “in” and “out” by interface

### IVideoDecoderManager (server API surface)

#### `IVideoDecoder.Id[] getVideoDecoderIds()`

**In:** no input parameters.  
**Out:** array of `IVideoDecoder.Id` parcelables. Each contains `int value`. `UNDEFINED=-1` exists but IDs returned here should be concrete resource IDs.  
**Implementation plan:** expose all platform decoder resources. Typical IDs start at 0 and increment by 1.

#### `OperationalMode[] getSupportedOperationalModes()`

**In:** none.  
**Out:** array of one or more `OperationalMode` enum values:
- `TUNNELLED`, `NON_TUNNELLED`, optional `GRAPHICS_TEXTURE`

**Implementation plan:** return the platform’s supported set; documentation states platform must support either tunnelled or non-tunnelled (both not required). Returned set must be stable between calls.

#### `@nullable IVideoDecoder getVideoDecoder(in IVideoDecoder.Id videoDecoderId)`

**In:** decoder ID.  
**Out:** `IVideoDecoder` binder interface instance for that resource ID, or null if invalid.

**Implementation plan:** create/hold server-side resource objects per ID and return a binder wrapper per resource.

### IVideoDecoder (server API surface)

#### `Capabilities getCapabilities()`

**In:** none.  
**Out:** `Capabilities` parcelable:
- `CodecCapabilities[] supportedCodecs`
- `DynamicRange[] supportedDynamicRanges`
- `boolean supportsSecure`

**Implementation plan:** provide a static capabilities object per decoder instance, stable across calls.

#### `@nullable PropertyValue getProperty(in Property property)`

**In:** `Property` enum key.  
**Out:** `PropertyValue` or null if unknown.

**Properties to support (from `Property.aidl`):**
- Read-only: `RESOURCE_ID`, `INPUT_QUEUE_DEPTH`, `OUTPUT_FRAME_POOL_SIZE`, `SECURE_VIDEO`, metrics (`METRIC_FRAMES_DECODED`, `METRIC_DECODE_ERRORS`, `METRIC_FRAMES_DROPPED`)
- Read-write: `OPERATIONAL_MODE` (write in READY, STARTED), `LOW_LATENCY_MODE` (write in READY), `DECODE_ERROR_POLICY` (write in READY), `AV_SOURCE` (write in READY), `SHA1_CALC` (write in READY, STARTED)

**Implementation plan:** maintain internal property store per open session where applicable (e.g., low latency, policy, SHA1) and per resource where applicable (e.g., resource id, queue depth, frame pool size, capabilities).

#### `boolean getPropertyMulti(in Property[] properties, out PropertyKVPair[] propertyKVList)`

**In:** non-empty array of `Property` keys.  
**Out:** `propertyKVList` array of same order and size, each element has:
- `Property property`
- `PropertyValue propertyValue`

**Error semantics (per AIDL doc):**
- Empty input is error: return false with `EX_ILLEGAL_ARGUMENT`.
- If any key invalid: return false with `EX_ILLEGAL_ARGUMENT`, and do not populate values.
- Null out-parameter: fail with `EX_NULL_POINTER`.

**Implementation plan:** implement atomic validation-first behavior: validate all keys, then fill output array.

#### `State getState()`

**In:** none.  
**Out:** current `com.rdk.hal.State`.

**Implementation plan:** state is per decoder resource session; closed/opened lifecycle controlled by `open/close/start/stop/flush`.

#### `@nullable IVideoDecoderController open(in Codec codec, in boolean secure, in IVideoDecoderControllerListener listener)`

**In:**
- `codec` enum (`Codec.aidl` includes MPEG2_VIDEO, H264_AVC, H265_HEVC, VP9, AV1, etc.)
- `secure` boolean (secure video path mode request)
- `listener` controller callbacks target (`IVideoDecoderControllerListener`)

**Out:** controller binder interface or null if unsupported codec or secure mode unsupported.

**Key required behaviors:**
- Only allowed from `State::CLOSED`.
- On success notify state transitions: `CLOSED -> OPENING -> READY`.
- If the client that opened the controller crashes, controller’s `stop()` and `close()` are implicitly called to perform cleanup.

**Implementation plan:**
- Validate codec support against `getCapabilities().supportedCodecs`.
- Validate `secure` against `getCapabilities().supportsSecure`.
- Create a controller session object with:
  - reference to decoder resource
  - stored `IVideoDecoderControllerListener` binder (for outputs)
  - per-session state and decode pipeline resources
- Establish binder death recipient for the controller client and on death:
  - if started, perform stop path (free pending input buffers, reset)
  - perform close path and transition to CLOSED (including callbacks)
- Persist `SECURE_VIDEO` property according to requested secure mode (read-only thereafter).

#### `boolean close(in IVideoDecoderController videoDecoderController)`

**In:** controller instance to close.  
**Out:** boolean success.

**Required behaviors:**
- Decoder must be `READY`.
- On success: state `READY -> CLOSING -> CLOSED`, with callbacks to event listeners.
- Returns false for invalid state or unrecognized parameter.

**Implementation plan:** verify controller matches current open session; reject unknown controller handles.

#### `boolean registerEventListener(in IVideoDecoderEventListener l)`

**In:** event listener binder object.  
**Out:** true if registered, false if already registered.

**Implementation plan:** maintain a set/list per decoder resource of registered listeners; ensure single registration per instance.

#### `boolean unregisterEventListener(in IVideoDecoderEventListener l)`

**In:** event listener binder object.  
**Out:** true if removed, false if not found.

### IVideoDecoderController (server API surface)

#### `void start()`

**In:** none.  
**Out:** none.

**Required behaviors:**
- Precondition `READY`.
- On success state `READY -> STARTING -> STARTED` and callbacks to event listeners.

**Implementation plan:** allocate/initialize decode pipeline as needed; ensure first output frame after start triggers metadata delivery rules described below.

#### `void stop()`

**In:** none.  
**Out:** none.

**Required behaviors:**
- Precondition `STARTED`.
- State `STARTED -> STOPPING -> READY`.
- Must free all submitted-but-not-decoded input AV buffers (same as flush effect).

**Implementation plan:** maintain a queue/list of submitted input buffer handles not yet released. On stop, release them all (see AV Buffer free path), reset internal decode pipeline state, counters reset requirements are not stated for stop but are stated for open/flush metrics.

#### `boolean setProperty(in Property property, in PropertyValue propertyValue)`

**In:** property key + value.  
**Out:** boolean success.

**Implementation plan:** enforce property-specific “write allowed in states” constraints as per `Property.aidl`. For `OPERATIONAL_MODE`, enforce mutual exclusivity between `TUNNELLED` and `NON_TUNNELLED`, and validate against `IVideoDecoderManager.getSupportedOperationalModes()`.

#### `boolean decodeBuffer(in long nsPresentationTime, in long bufferHandle)`

**In:**
- `nsPresentationTime` presentation timestamp in nanoseconds
- `bufferHandle` AV buffer handle containing encoded single-frame elementary stream data

**Out:** boolean:
- true on success
- false if decode buffer is full

**Required behaviors:**
- Precondition `STARTED`.
- Each call references a single video frame with PTS.
- Once decoder finishes processing the buffer, it is automatically released and returned to AV Buffer Manager; caller must not modify/free after submission.
- Video decoder outputs decoded frames in presentation order.
- In non-tunnelled mode, decoded frames are delivered to client via `IVideoDecoderControllerListener.onFrameOutput()` with a frame buffer handle.
- In tunnelled mode, frames are not delivered as frame buffer handles; `onFrameOutput()` calls must have `frameBufferHandle=-1` when made for metadata-only purposes.

**Implementation plan (API-wise ownership):**
- On accepting `decodeBuffer()`, the server takes ownership of `bufferHandle`.
- The server must call `IAVBuffer.free(bufferHandle)` after decoding is complete, or if the buffer is dropped due to flush/stop.
- If internal input queue is full, return false without taking ownership (do not free in that case).

#### `void flush(in boolean reset)`

**In:** reset flag.  
**Out:** none.

**Required behaviors:**
- Precondition `STARTED`.
- Free any queued input AV buffers not yet decoded.
- Return pending decoded frames due for callback back to vendor frame pool.
- Optionally reset internal decoder state fully to opened `READY` state when `reset=true`.
- State transitions per doc example: `STARTED -> FLUSHING -> STARTED`, with event callbacks.

**Implementation plan:** implement flush as an operation that:
- Blocks further decode submissions or returns false until flush completes, depending on queueing strategy.
- Produces state transition callbacks.
- Clears discontinuity/EOS pending markers appropriately so next output metadata adheres to “first frame after flush” metadata requirement.

#### `void signalDiscontinuity()`

**In:** none.  
**Out:** none.

**Required behavior:**
- Precondition `STARTED`.
- Buffers following this call in `decodeBuffer()` must be regarded as PTS-discontinuous.
- First output after discontinuity must indicate discontinuity in the next output `FrameMetadata`.

**Implementation plan:** store a “discontinuity pending” marker in the session. On the next output callback where metadata is delivered, set `FrameMetadata.discontinuity=true` and clear marker.

#### `void signalEOS()`

**In:** none.  
**Out:** none.

**Required behavior:**
- Precondition `STARTED`.
- No more AV buffers expected after `signalEOS()` unless flushed or stopped and started again.
- After all frames output, emit a `FrameMetadata` with `endOfStream=true` via `onFrameOutput()` after all frames have been output.

**Implementation plan:** store an “EOS pending” marker once called. When the decode pipeline drains:
- Ensure a final metadata delivery occurs with `FrameMetadata.endOfStream=true`.
- The API requires the callback; in tunnelled mode, this will be a metadata-only `onFrameOutput()` with `frameBufferHandle=-1` and `nsPresentationTime` as appropriate for the stream (the doc states `nsPresentationTime` can be `-1` if only metadata is being returned per controller listener AIDL; however EOS is tied to frame ordering, so plan must follow the `IVideoDecoderControllerListener.onFrameOutput()` signature rules and use either the last PTS or `-1` depending on vendor behavior while remaining within the allowed signature constraints).

#### `boolean parseCodecSpecificData(in CSDVideoFormat csdVideoFormat, in byte[] codecData)`

**In:**
- `csdVideoFormat` enum: `AVC_DECODER_CONFIGURATION_RECORD`, `HEVC_DECODER_CONFIGURATION_RECORD`, `AV1_DECODER_CONFIGURATION_RECORD`
- `codecData` byte array, must not be empty, and must match codec chosen in `open()`

**Out:** boolean success.

**Required behavior:**
- Precondition `STARTED`.
- Must be called before `decodeBuffer()` when required by media.

**Implementation plan:** store and apply codec-specific data to the decode pipeline. Validate:
- non-empty codecData
- format matches open codec type

## API-wise “out” behavior: callbacks

### Event listener callbacks (IVideoDecoderEventListener)

#### `onStateChanged(oldState, newState)` (oneway)

**Trigger points (from docs and sequence example):**
- open: `CLOSED->OPENING`, `OPENING->READY`
- start: `READY->STARTING`, `STARTING->STARTED`
- flush: `STARTED->FLUSHING`, `FLUSHING->STARTED`
- stop: `STARTED->STOPPING`, `STOPPING->READY`
- close: `READY->CLOSING`, `CLOSING->CLOSED`

**Implementation plan:** ensure ordering and that transitions correspond to state machine. Since listener interface is oneway, callbacks are asynchronous; maintain internal ordering by sending in sequence from the same thread/queue.

#### `onDecodeError(errorCode, vendorErrorCode)` (oneway)

**Error model:**
- Uses `ErrorCode` enum and vendor-specific `int vendorErrorCode`.
- `ErrorCode.aidl` currently contains a placeholder `xxxx = 1` and indicates it is intended to be extended.

**Implementation plan:** still implement callback emission on any decode error. The vendorErrorCode is used to convey detailed platform code.

### Controller listener callbacks (IVideoDecoderControllerListener)

#### `onFrameOutput(nsPresentationTime, frameBufferHandle, @nullable FrameMetadata metadata)` (oneway)

**Key rules from AIDL and HALIF docs:**
- Called when a full video frame has been decoded or metadata needs notification.
- `metadata` must be non-null:
  - on first frame after `start()` or `flush()`
  - when metadata changes in the stream
- `metadata` may be null only if contents have not changed since last callback.
- `nsPresentationTime`:
  - is the frame presentation time in nanoseconds for frame output
  - may be `-1` if only metadata is being returned (per controller listener AIDL comment)
- `frameBufferHandle`:
  - in non-tunnelled mode: handle to 2D frame buffer
  - tunnelled mode: set to `-1` (no buffer delivered)
  - also set to `-1` if no handle delivered and the call is for metadata-only purposes

**Implementation plan by operational mode:**

1. Non-tunnelled mode:
   - For each decoded frame, issue `onFrameOutput(pts, frameBufferHandle, metadataOrNull)`.
   - Frame buffer handles must be globally unique handles compatible with `IAVBuffer.free()` (HALIF AV Buffer doc requires uniqueness across vendor frame pools and heaps).
   - Client will later pass that handle downstream (e.g., to a sink) and free it when no longer needed; the AV Buffer HAL describes that in non-tunnelled mode client may still have these handles during stop/flush, and must free via `IAVBuffer.free()`.

2. Tunnelled mode:
   - Do not deliver frame buffer handles (always `-1`).
   - Still deliver metadata when required (first frame after start/flush, metadata changes, EOS marker, discontinuity marker).
   - HALIF doc states: if operating exclusively in tunnelled mode, and there is no metadata to pass, then no call to `onFrameOutput()` should be made.

**Metadata content to populate (from `FrameMetadata.aidl`):**
- Pixel aspect ratio: `parX`, `parY`
- Source aspect ratio: `sarX`, `sarY`
- Dimensions: `codedWidth`, `codedHeight`, `activeX`, `activeY`, `activeWidth`, `activeHeight`
- `colorDepth`
- `pixelFormat` (enum)
- `dynamicRange` (enum)
- `scanType` (enum)
- `afd`
- `frameRateNumerator`, `frameRateDenominator`
- `endOfStream` boolean (must be set true on final EOS callback)
- `discontinuity` boolean (first output after `signalDiscontinuity()`)
- `lowLatency` boolean (reflect low latency mode)
- `source` (`com.rdk.hal.AVSource`) aligned with `Property.AV_SOURCE`
- `sha1` byte array (when `Property.SHA1_CALC` is enabled)
- `extension` (`ParcelableHolder`)

**Implementation plan:**
- Track last-sent metadata snapshot. Decide if metadata changed; if not, pass null in subsequent frame callbacks (except where explicitly required to send non-null).
- Ensure “first frame after start” and “first frame after flush” always includes non-null metadata.
- When `SHA1_CALC` is enabled, populate `sha1` field for decoded frames.

#### `onUserDataOutput(nsPresentationTime, userData)` (oneway)

**Rules:**
- Delivers picture user data from a frame.
- Must be in same frame presentation order as output frames.
- Output frame may be delivered before or after the user data callback.

**Implementation plan:**
- If user data is detected/extracted by decoder pipeline, issue callback with PTS matching the frame.
- Ensure ordering matches presentation order.

## AV Buffer integration (handles, pools, freeing)

### Input buffers (encoded video elementary stream)

**Client responsibility (from docs):**
- Allocate buffers from AV Buffer pools (`IAVBuffer.createVideoPool()`, `alloc()`), fill data, and pass handle to `decodeBuffer()`.
- Must not modify/free after submission.

**Server responsibility:**
- Once buffer is processed, it is automatically released and returned to AV Buffer Manager:
  - Call `IAVBuffer.free(bufferHandle)` after decode completes.
- If `stop()` or `flush()` occurs:
  - Free any outstanding submitted-but-not-decoded input buffer handles using `IAVBuffer.free()`.

### Pool creation constraints (createVideoPool)

`IAVBuffer.createVideoPool(secureHeap, IVideoDecoder.Id videoDecoderId, IAVBufferSpaceListener listener)`

**Important API constraints:**
- `videoDecoderId` must have been obtained from `IVideoDecoderManager.getVideoDecoderIds()`.
- If invalid ID: `EX_ILLEGAL_ARGUMENT`.
- Out of memory: `EX_SERVICE_SPECIFIC` with `HALError::OUT_OF_MEMORY`.
- Pool handle type is `byte` (`Pool.handle`), and `Pool.INVALID_POOL=-1`.

**Space available callback:**
- `IAVBuffer.notifyWhenSpaceAvailable(poolHandle, size)` triggers `IAVBufferSpaceListener.onSpaceAvailable()` when enough space becomes available.

### Frame buffer handles (decoded output frames) in non-tunnelled mode

HALIF AV Buffer documentation states:
- AV buffer handles must be unique across all pools/heaps including vendor video frame pool.
- Video and audio frame pools are managed entirely by vendor layer, but handles must share the same handle space so any handle can be passed to `IAVBuffer.free()`.

**Implementation plan:**
- Implement a vendor-side decoded frame pool allocator that issues handles compatible with `IAVBuffer.free()`.
- Ensure that on frame lifecycle completion (downstream in pipeline), handles are freed via `IAVBuffer.free()`.

## Operational modes (property controlled)

### Discover operational modes

- Client reads `IVideoDecoderManager.getSupportedOperationalModes()`.
- Client sets `Property.OPERATIONAL_MODE` via `IVideoDecoderController.setProperty()` (type Integer in `PropertyValue`).

### Rules from HALIF doc

- At least one of `TUNNELLED` or `NON_TUNNELLED` must be supported.
- `TUNNELLED` and `NON_TUNNELLED` are mutually exclusive.
- `GRAPHICS_TEXTURE` may be ORed with either.
- Modes may be changed while in `READY` or `STARTED`.

**Implementation plan:**
- Enforce mutual exclusion when setting OPERATIONAL_MODE.
- On operational mode change, ensure output path matches new mode (handle vs tunnelled).
- Maintain metadata callback rules regardless of mode.

## Timing and ordering rules

### Presentation time base

- PTS base is nanoseconds (`long`).
- `decodeBuffer(nsPresentationTime, bufferHandle)` uses that PTS.
- `onFrameOutput(nsPresentationTime, ...)` must use the same time base.

### Output ordering

- Video decoder shall output frames in presentation order regardless of input order (input is encoder order).

**Implementation plan:**
- Implement frame re-ordering for codecs with B-frames where needed, except in low latency mode where B-frames may be absent or skipped (per property description and HALIF low-latency note).

## Detailed call flow sequences (API-wise)

### Sequence: typical non-tunnelled playback

1. Client gets service `IVideoDecoderManager`.
2. Client calls `getVideoDecoderIds()` and selects an ID.
3. Client calls `getVideoDecoder(id)` to obtain `IVideoDecoder`.
4. Client registers event listener:
   - `registerEventListener(eventListener)`
5. Client calls `open(codec, secure, controllerListener)`:
   - Server sends `onStateChanged(CLOSED, OPENING)` then `onStateChanged(OPENING, READY)`.
   - Server returns controller.
6. Client calls controller `start()`:
   - Server sends `onStateChanged(READY, STARTING)`, `onStateChanged(STARTING, STARTED)`.
7. Client repeatedly calls `decodeBuffer(pts, bufferHandle)`:
   - Server eventually calls:
     - `onFrameOutput(pts, frameBufferHandle, metadataOrNull)`
   - Server frees encoded input handle by calling `IAVBuffer.free(bufferHandle)`.
8. Client may call `signalDiscontinuity()` between buffers:
   - Server sets `FrameMetadata.discontinuity=true` in next metadata-delivering output.
9. Client may call `signalEOS()`:
   - Server drains and emits `FrameMetadata.endOfStream=true` after all output.
10. Client calls `stop()`:
   - Server transitions `STARTED->STOPPING->READY`, frees pending input buffers.
11. Client calls `close(controller)`:
   - Server transitions `READY->CLOSING->CLOSED`.
12. Client unregisters listener:
   - `unregisterEventListener(eventListener)`

### Sequence: flush while started

1. Client in `STARTED`.
2. Client calls `flush(reset)`:
   - Server transitions `STARTED->FLUSHING->STARTED`.
   - Server frees pending input buffers.
   - Server returns pending decoded frames to vendor pool.
3. Next output after flush must include non-null metadata per metadata rules.

## Cleanup on client crash (controller owner death)

The `IVideoDecoder.open()` contract states:

- If the client that opened the controller crashes, the controller has `stop()` and `close()` implicitly called to perform clean up.

**Implementation plan:**
- Register binder death recipient on the controller’s owning binder client.
- On death:
  - If in STARTED, execute stop behavior (including freeing pending input buffers).
  - Ensure state transitions are consistent and resource returns to CLOSED (via close path), including `onStateChanged()` notifications to registered event listeners.
  - Release controller and any per-session resources.

## Required minimum data structures (API-wise)

To satisfy the API contracts, the server needs to track:

- Decoder resource table:
  - `IVideoDecoder.Id` -> resource object
  - immutable `Capabilities`
  - list of registered `IVideoDecoderEventListener` callbacks
  - current `State`

- Open session (per resource, at most one at a time):
  - `Codec` selected in open()
  - secure mode flag
  - `IVideoDecoderControllerListener` binder reference
  - property store (operational mode, low latency, decode error policy, AV source, SHA1 calc)
  - pending input buffer handles (accepted via decodeBuffer but not yet freed)
  - discontinuity pending marker
  - EOS pending marker
  - last-sent `FrameMetadata` snapshot (to decide metadata==null vs non-null)

- Output frame buffer pool handle strategy (non-tunnelled mode):
  - provide globally unique handles compatible with `IAVBuffer.free()`.

## Notes strictly derived from the provided sources

- Encoded buffers are passed one frame at a time and freed by the decoder when finished (Video Decoder AIDL and HALIF doc).
- AV Buffer handles must be globally unique across pools/heaps including vendor frame pools (AV Buffer HALIF doc).
- When STOPPING or FLUSHING, the session must free any AV buffers it is holding (Video Decoder HALIF doc, “Video Decoder States” section).
- In tunnelled mode, `onFrameOutput()` is used for metadata delivery (with `frameBufferHandle=-1`), and if no metadata is to be passed, no callback should be made (Video Decoder HALIF doc, “Frame Metadata” section).
- Discontinuity and EOS behavior must be signaled via `FrameMetadata.discontinuity` and `FrameMetadata.endOfStream` (Video Decoder HALIF doc).

