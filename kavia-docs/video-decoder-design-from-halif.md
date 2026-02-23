<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder_documents/video-decoder-design-from-halif.md
- Operation: write
- Timestamp: 2026-02-12T04:54:17.800024+00:00
- Restored At: 2026-02-23T05:04:11.252027+00:00
- Task ID: cm219d4578
-->

# Video Decoder Design (HALIF + AIDL Derived)

## Scope and sources

This design document is derived strictly from the following sources:

- HALIF Video Decoder documentation (including Video Decoder states): `rdk-halif-aidl-17/docs/halif/video_decoder/current/video_decoder.md`
- HALIF AV Buffer documentation: `rdk-halif-aidl-17/docs/halif/av_buffer/current/av_buffer.md`
- Video Decoder AIDL definitions: `rdk-halif-aidl-17/videodecoder/current/com/rdk/hal/videodecoder/*.aidl`
- AV Buffer AIDL definitions: `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/*.aidl`
- Common HAL state enum: `rdk-halif-aidl-17/common/current/com/rdk/hal/State.aidl`

No other sources are used.

## Overview

The Video Decoder HAL service provides interfaces for passing compressed video to the vendor layer for decoding. The decoded output supports:

1. Non-tunnelled mode, where decoded video is returned to the client as frame buffer handles along with metadata.
2. Tunnelled mode, where decoded video is passed directly through the vendor layer and frames are not returned as buffers to the client, while metadata is still returned (subject to the rules described below).

The operational mode is selected by the client and controlled using the `Property.OPERATIONAL_MODE` property. The Video Decoder interfaces are accessed via a manager service (`IVideoDecoderManager`) which provides one or more decoder instances (`IVideoDecoder`). Opening a decoder yields a controller sub-interface (`IVideoDecoderController`) used for start/stop/flush and feeding coded AV buffers for decode.

AV buffers (secure and non-secure) are managed by the AV Buffer HAL (`IAVBuffer`) using globally unique handles.

## Key interfaces and roles

### Video Decoder Manager: `IVideoDecoderManager`

The manager is a Binder-published service (`IVideoDecoderManager.serviceName = "VideoDecoderManager"`) that exposes:

- `getVideoDecoderIds() -> IVideoDecoder.Id[]` to enumerate available decoder resources.
- `getSupportedOperationalModes() -> OperationalMode[]` to declare platform supported operational modes.
- `getVideoDecoder(IVideoDecoder.Id) -> @nullable IVideoDecoder` to obtain a decoder instance interface.

The supported operational modes are system-wide and must apply to all video decoder instances. The returned set must not change between calls.

### Video Decoder instance: `IVideoDecoder`

An `IVideoDecoder` represents a single video decoder resource. It exposes:

- Capabilities and properties:
  - `getCapabilities() -> Capabilities`
  - `getProperty(Property) -> @nullable PropertyValue`
  - `getPropertyMulti(Property[] properties, out PropertyKVPair[] propertyKVList) -> boolean`
  - `getState() -> State`
- Lifecycle:
  - `open(Codec codec, boolean secure, IVideoDecoderControllerListener listener) -> @nullable IVideoDecoderController`
  - `close(IVideoDecoderController controller) -> boolean`
- Events:
  - `registerEventListener(IVideoDecoderEventListener) -> boolean`
  - `unregisterEventListener(IVideoDecoderEventListener) -> boolean`

A decoder can only be opened when in `State.CLOSED`. Close requires `State.READY`. The open call configures the codec and secure mode for the session and returns the controller interface used to drive decoding.

If the client that opened the controller crashes, the controller has `stop()` and `close()` implicitly called to perform cleanup.

### Video Decoder controller: `IVideoDecoderController`

The controller interface drives the session:

- `start()` transitions the decoder from `READY -> STARTING -> STARTED`.
- `stop()` transitions `STARTED -> STOPPING -> READY` and frees any queued but not-yet-decoded input buffers, effectively acting like a flush.
- `flush(boolean reset)` transitions `STARTED -> FLUSHING -> STARTED`, frees queued input buffers, returns any pending decoded frames to the vendor frame buffer pool, and optionally resets internal decoder state back to the opened “ready” baseline depending on `reset`.
- `decodeBuffer(long nsPresentationTime, long bufferHandle) -> boolean` submits one coded frame in an AV buffer handle for decode. Ownership is transferred to the decoder; the decoder later releases the AV buffer back to AV Buffer.
- `signalDiscontinuity()` marks a PTS discontinuity between buffers delivered via `decodeBuffer()`.
- `signalEOS()` indicates end of stream after the final `decodeBuffer()` has been called; the decoder must drain previously submitted buffers and then output metadata with `endOfStream=true`.
- `parseCodecSpecificData(CSDVideoFormat csdVideoFormat, byte[] codecData) -> boolean` provides out-of-band codec specific data while in `STARTED` state, before coded frame buffers when required.

### Listener callbacks

#### Controller listener: `IVideoDecoderControllerListener`

The controller calls back to the client (oneway):

- `onFrameOutput(long nsPresentationTime, long frameBufferHandle, @nullable FrameMetadata metadata)`
- `onUserDataOutput(long nsPresentationTime, byte[] userData)`

`onFrameOutput()` is used both for frame output (non-tunnelled) and for metadata delivery (all modes), with sentinel values:
- `frameBufferHandle = -1` indicates no frame buffer handle is delivered (e.g. tunnelled mode or metadata-only callback).
- `nsPresentationTime = -1` indicates only metadata is being returned (per the interface comment).

Metadata delivery rules are described in the HALIF Video Decoder doc and apply to all operational modes.

#### Event listener: `IVideoDecoderEventListener`

The decoder calls back to the client (oneway):

- `onDecodeError(ErrorCode errorCode, int vendorErrorCode)`
- `onStateChanged(State oldState, State newState)`

## Operational modes

Operational modes are described by the `OperationalMode` enum:

- `TUNNELLED = 1 << 0`
- `NON_TUNNELLED = 1 << 1`
- `GRAPHICS_TEXTURE = 1 << 2`

The HALIF Video Decoder documentation states:

- Tunnelled and non-tunnelled modes cannot operate at the same time.
- Supporting both is optional, but at least one of tunnelled or non-tunnelled must be supported.
- If both are supported, there shall never be a dynamic switch between tunnelled and non-tunnelled while `STARTED`.
- The `Property.OPERATIONAL_MODE` property controls the selected operational mode.
- The video decoder may switch operational modes at any time while in a `READY` or `STARTED` state.
- `GRAPHICS_TEXTURE` is optional and may run concurrently with tunnelled or non-tunnelled when supported.

### Expected output behavior per mode

In all modes, coded video frames are passed into the decoder via `decodeBuffer()` using AV buffer handles.

In non-tunnelled mode:
- Decoded video frames are returned via `IVideoDecoderControllerListener.onFrameOutput()` as `frameBufferHandle` values.
- Frames must be delivered in presentation order.

In tunnelled mode:
- Decoded frames are passed directly to the linked video sink/plane for rendering.
- `onFrameOutput()` calls are not used to deliver frame buffers; when `onFrameOutput()` must be called (for metadata), `frameBufferHandle` must be `-1`.
- If there is no frame metadata to deliver while operating exclusively in tunnelled mode, then no `onFrameOutput()` call should be made (because there is neither frame buffer nor metadata to return).

## Frame metadata

### `FrameMetadata` structure

The `FrameMetadata` parcelable includes (non-exhaustive listing; see AIDL for full definitions):

- Aspect and dimensions:
  - `parX`, `parY` (pixel aspect ratio)
  - `sarX`, `sarY` (source aspect ratio)
  - `codedWidth`, `codedHeight`
  - `activeX`, `activeY`, `activeWidth`, `activeHeight`
- Format and video characteristics:
  - `colorDepth`
  - `pixelFormat` (`PixelFormat`)
  - `dynamicRange` (`DynamicRange`)
  - `scanType` (`ScanType`)
  - `afd` (active format description)
  - `frameRateNumerator`, `frameRateDenominator`
- Stream markers and mode indicators:
  - `endOfStream`
  - `discontinuity`
  - `lowLatency`
- Source and debug:
  - `source` (`AVSource`)
  - `sha1` (only set when SHA1 calculation is enabled)
  - `extension` (`ParcelableHolder`)

### When metadata is delivered

The HALIF Video Decoder documentation defines:

- Metadata is passed over `IVideoDecoderControllerListener.onFrameOutput()`.
- In non-tunnelled mode, the frame buffer handle and frame metadata are passed in the same `onFrameOutput()` call.
- To conserve CPU load, metadata is only passed:
  1. With the first decoded frame after `start()`,
  2. With the first decoded frame after `flush()`,
  3. When the metadata changes.
- If metadata does not need to be passed, the `metadata` argument should be `null` in `onFrameOutput()`.

These rules apply to all operational modes.

## Stream discontinuities and end-of-stream

### Discontinuities

Where the client has knowledge of PTS discontinuities in the video stream, it calls `IVideoDecoderController.signalDiscontinuity()` between buffers delivered via `decodeBuffer()`. For the first input AV buffer passed for decode after the discontinuity, the next output `FrameMetadata` shall indicate the discontinuity.

### End of stream

When the client has delivered the final coded frame via `decodeBuffer()`, it calls `IVideoDecoderController.signalEOS()`.

The decoder must continue decoding any previously submitted buffers. After all video frames have been output from the decoder, it must emit `FrameMetadata` with `endOfStream=true`.

After `signalEOS()`, no further coded buffers are expected unless the decoder is flushed or stopped and started again.

## AV Buffer integration

### AV Buffer responsibilities

The AV Buffer HAL manages secure and non-secure memory heaps and pools. AV memory buffers are referenced by handles and passed across HAL interfaces.

Key AV Buffer requirements from the HALIF AV Buffer documentation include:

- AV buffer handles must be globally unique across all memory pools and heaps, including vendor video frame pool and audio frame pool.
- Secure buffers cannot be mapped into unprivileged processes, and only vendor-trusted entities can access secure memory.
- A helper library is provided for mapping/unmapping non-secure buffers and copying data to secure and non-secure buffers.

### Pools and allocations: `IAVBuffer`

`IAVBuffer` is a service (`IAVBuffer.serviceName = "AVBuffer"`) that supports:

- Heap metrics:
  - `getHeapMetrics(boolean secureHeap) -> HeapMetrics`
- Pool creation and lifecycle:
  - `createVideoPool(boolean secureHeap, IVideoDecoder.Id videoDecoderId, IAVBufferSpaceListener listener) -> Pool`
  - `createAudioPool(boolean secureHeap, IAudioDecoder.Id audioDecoderId, IAVBufferSpaceListener listener) -> Pool`
  - `destroyPool(Pool poolHandle) -> boolean`
- Pool metrics:
  - `getPoolMetrics(Pool poolHandle) -> PoolMetrics`
  - `getAllPoolMetrics(boolean secureHeap) -> PoolMetrics[]`
- Buffer allocation and lifecycle:
  - `alloc(Pool poolHandle, int size) -> long`
  - `trimSize(long bufferHandle, int newSize) -> boolean`
  - `free(long bufferHandle) -> boolean`
  - `isValid(long bufferHandle) -> boolean`
  - `getAllocList(Pool poolHandle) -> long[]`
- Space-available notification:
  - `notifyWhenSpaceAvailable(Pool poolHandle, int size) -> boolean`

Allocation failures due to out-of-memory are reported via service-specific errors and the client can request callbacks via `notifyWhenSpaceAvailable()`.

### Ownership and freeing rules relevant to Video Decoder

From the `IVideoDecoderController.decodeBuffer()` definition and the HALIF Video Decoder states description:

- The client submits coded buffers by AV buffer handle and must not modify or free the buffer after submission.
- Once the decoder finishes processing a coded buffer, it automatically releases it and returns it to the AV Buffer Manager.
- When a Video Decoder session enters `FLUSHING` or `STOPPING`, it shall free any AV buffers it is holding.

### Decoded video frame buffers and AV Buffer

The HALIF Video Decoder documentation describes decoded frame buffers in non-tunnelled mode:

- Frame buffers are passed back as `frameBufferHandle` values in `onFrameOutput()`.
- If the input coded buffer was secure, the corresponding decoded frame must be output in a secure video frame buffer.
- The vendor layer manages the pool of decoded frame buffers privately and reports its size via `Property.OUTPUT_FRAME_POOL_SIZE`.
- If the output frame buffer pool is empty, frame output is blocked until a new frame buffer is available. During blockage, the decoder may buffer additional coded inputs or reject new `decodeBuffer()` calls (returning `false`).

The AV Buffer documentation further states that video frame pools are managed privately inside the vendor layer by the decoders, but frame buffer handles must share the same handle space such that `IAVBuffer.free()` can free them. In non-tunnelled mode, if the client still holds frame buffer handles during stop/flush, it must free them using `IAVBuffer.free()`.

## Session state model and transitions

### State enum

The standard HAL session state enum is `com.rdk.hal.State`:

- `UNKNOWN`, `CLOSED`, `OPENING`, `READY`, `STARTING`, `STARTED`, `FLUSHING`, `STOPPING`, `CLOSING`

### Video Decoder state behavior

The Video Decoder follows the standard Session State Management paradigm as documented in the HALIF Video Decoder doc.

Key lifecycle constraints from AIDL:

- `IVideoDecoder.open()` requires `State.CLOSED` and transitions through `OPENING` to `READY` on success, notifying `IVideoDecoderEventListener.onStateChanged(...)`.
- `IVideoDecoderController.start()` requires `State.READY` and transitions through `STARTING` to `STARTED`.
- `IVideoDecoderController.flush(reset)` requires `State.STARTED` and transitions through `FLUSHING` back to `STARTED`, freeing queued input AV buffers and returning pending decoded frames to the frame pool.
- `IVideoDecoderController.stop()` requires `State.STARTED` and transitions through `STOPPING` to `READY`, freeing queued input AV buffers.
- `IVideoDecoder.close()` requires `State.READY` and transitions through `CLOSING` to `CLOSED`.

### Callback/state sequence for typical usage (from HALIF documentation)

The following sequence is based on the sequence diagram in the HALIF Video Decoder states section:

1. Client registers `IVideoDecoderEventListener`.
2. Client calls `IVideoDecoder.open(...)`:
   - State change `CLOSED -> OPENING` is notified.
   - Controller is created.
   - State change `OPENING -> READY` is notified.
   - Client receives `IVideoDecoderController`.
3. Client calls `IVideoDecoderController.start()`:
   - State changes `READY -> STARTING -> STARTED` are notified.
4. Client calls `decodeBuffer(...)` for coded AV buffers:
   - Decoder outputs frames/metadata via controller listener.
   - Decoder frees input coded AV buffers via `IAVBuffer.free(...)` after processing.
5. Client calls `flush(...)`:
   - State change `STARTED -> FLUSHING` is notified.
   - Decoder frees queued input buffers it is holding.
   - State change `FLUSHING -> STARTED` is notified.
6. Client calls `stop()`:
   - State change `STARTED -> STOPPING` is notified.
   - Decoder frees queued input buffers it is holding.
   - State change `STOPPING -> READY` is notified.
7. Client calls `close(controller)`:
   - State change `READY -> CLOSING -> CLOSED` is notified.
   - Controller is deleted.
8. Client unregisters event listener.

## Video decoder properties relevant to design

The `Property` enum defines keys used with `getProperty()` / `setProperty()`.

Properties and constraints explicitly defined in AIDL include:

- `RESOURCE_ID` (read-only integer): unique per decoder resource instance.
- `INPUT_QUEUE_DEPTH` (read-only integer): input queue size in bytes.
- `OUTPUT_FRAME_POOL_SIZE` (read-only integer): number of frame buffers in vendor pool.
- `OPERATIONAL_MODE` (read-write integer): controls tunnelled/non-tunnelled/graphics texture selection.
  - `TUNNELLED` and `NON_TUNNELLED` are mutually exclusive.
  - Write allowed in states: `READY`, `STARTED`.
- `LOW_LATENCY_MODE` (read-write integer 0/1): intended for low latency operation, settable in `READY`.
- `DECODE_ERROR_POLICY` (read-write integer): error concealment policy, settable in `READY`.
- `AV_SOURCE` (read-write integer, `AVSource`): settable in `READY`, also reflected in `FrameMetadata.source`.
- `SHA1_CALC` (read-write integer 0/1): enables SHA1 calculation and returns result in metadata, settable in `READY` and `STARTED`.
- `SECURE_VIDEO` (read-only integer 0/1): indicates whether decoder was opened for secure video path.
- Metrics (read-only integers; may return `-1` if not implemented):
  - `METRIC_FRAMES_DECODED`
  - `METRIC_DECODE_ERRORS`
  - `METRIC_FRAMES_DROPPED`

## Capabilities and secure video processing

### Capabilities

`Capabilities` includes:

- `CodecCapabilities[] supportedCodecs`
- `DynamicRange[] supportedDynamicRanges`
- `boolean supportsSecure`

`IVideoDecoder.getCapabilities()` can be called at any time and must return a value that does not change between calls.

### Secure video processing (SVP) constraints

The HALIF Video Decoder documentation states:

- Video decoder instances declare secure support via `Capabilities.supportsSecure`.
- Secure video decoder instances handle secure AV buffers and decoded frames must be in secure frame buffers or securely tunnelled.

From `IVideoDecoder.open(codec, secure, ...)`:
- The call returns null if the codec or requested secure mode is not supported.
- If a secure coded input buffer is provided, the decoded output must remain secure.

## Presentation time base

Presentation time units are nanoseconds and represented as `long` (int64) in AIDL.

- The `nsPresentationTime` parameter to `decodeBuffer()` represents the presentation time for that coded frame.
- The `nsPresentationTime` used in `onFrameOutput()` must use the same time base.
- The decoder outputs frames in presentation order, regardless of input order.

## User data output

The controller listener includes `onUserDataOutput(nsPresentationTime, userData)`:

- The user data is delivered in the same frame presentation order as output frames.
- The output video frame can be delivered before or after the user data callback.
- The userData format is described only as “starts from (and includes) the user_identifier field (TBC)” in the AIDL comment.

## Appendix: state transition summary table

The following table summarizes state transitions described by the AIDL comments and HALIF states section:

| Operation | Preconditions | Transitions (notified via `onStateChanged`) | Buffer freeing expectations |
|---|---|---|---|
| `open()` | `CLOSED` | `CLOSED -> OPENING -> READY` | N/A |
| `start()` | `READY` | `READY -> STARTING -> STARTED` | N/A |
| `decodeBuffer()` | `STARTED` | No state change required | Decoder takes ownership of coded AV buffer and frees it after processing |
| `flush(reset)` | `STARTED` | `STARTED -> FLUSHING -> STARTED` | Decoder frees queued coded input buffers it holds; returns pending decoded frames to frame pool |
| `stop()` | `STARTED` | `STARTED -> STOPPING -> READY` | Decoder frees queued coded input buffers it holds (flush-like behavior) |
| `close()` | `READY` | `READY -> CLOSING -> CLOSED` | Controller destroyed |

