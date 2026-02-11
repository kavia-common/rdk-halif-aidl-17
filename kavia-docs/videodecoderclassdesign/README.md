# Video Decoder Class Design — Stub C++ Skeleton

This folder documents the generated **stub C++ project structure** and **class skeletons** for a Video Decoder design aligned with:

- HAL AIDL interfaces under:
  - `videodecoder/current/com/rdk/hal/videodecoder/*`
  - `avbuffer/current/com/rdk/hal/avbuffer/*`
- The repository’s existing module layout pattern (e.g., `avbuffer/current/` contains helper headers and interface definitions).

## Scope

This is intentionally a **non-functional stub**:
- No binder registration is implemented.
- No platform decode, buffer mapping, or threading is implemented.
- Types are kept minimal and are intended to be replaced with real generated-AIDL C++ types or platform equivalents.

The goal is to provide:
- A clean folder structure
- Clear class boundaries
- Method stubs whose naming/intent aligns to the AIDL APIs

## New source folder

Stubs are placed here:

- `videodecoderclassdesign/src/videodecoder/include/rdk/hal/videodecoder/`
- `videodecoderclassdesign/src/videodecoder/src/`

This keeps the design artifacts separate from the AIDL interface definitions while still living in the same repo.

## Mapping to AIDL

### Manager (AIDL: `IVideoDecoderManager`)
AIDL responsibilities:
- `getVideoDecoderIds()`
- `getSupportedOperationalModes()`
- `getVideoDecoder(id)`

Stub class:
- `rdk::hal::videodecoder::VideoDecoderManager`

### Decoder (AIDL: `IVideoDecoder`)
AIDL responsibilities:
- `getCapabilities()`
- `getProperty()`, `getPropertyMulti()`
- `getState()`
- `open(codec, secure, controllerListener)`
- `close(controller)`
- `registerEventListener()`, `unregisterEventListener()`

Stub class:
- `rdk::hal::videodecoder::VideoDecoder`

### Controller (AIDL: `IVideoDecoderController`)
AIDL responsibilities:
- `start()`, `stop()`
- `setProperty()`
- `decodeBuffer(nsPts, bufferHandle)`
- `flush(reset)`
- `signalDiscontinuity()`
- `signalEOS()`
- `parseCodecSpecificData(format, data)`

Stub class:
- `rdk::hal::videodecoder::VideoDecoderController`

### AVBuffer integration points

AIDL `IVideoDecoderController.decodeBuffer(...)` consumes an **AVBuffer handle** (`long bufferHandle` in AIDL).
This stub models buffer handles as `uint64_t`.

In a real implementation, the controller would:
- validate handles (via AVBuffer service)
- map/unmap handle memory (potentially via an `IAVBufferHelper`-style helper similar to `avbuffer/current/avbufferhelper.h`)
- submit elementary stream data to the hardware decoder pipeline

## Next steps (expected future work)

- Replace stub types in `types.h` with generated AIDL C++ types (Ndk/Cpp backend) or a thin adapter layer.
- Integrate binder glue and service registration.
- Add real state machine + listener callback plumbing.
- Add AVBuffer helper dependency for mapping/unmapping handles.

"""
"""
