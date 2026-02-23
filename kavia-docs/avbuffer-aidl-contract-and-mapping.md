<!--
MongoDB Document Metadata:
- Original File Path: 10002556%2FTEVDevice/docs/avbuffer-aidl-contract-and-mapping.md
- Operation: write
- Timestamp: 2026-02-19T08:51:07.028474+00:00
- Restored At: 2026-02-23T05:04:11.253190+00:00
- Task ID: cm219d4578
-->

# AVBuffer AIDL Contract (HALIF) and VDevice_AVBuffer Mapping

## Purpose

This document summarizes the HALIF AVBuffer AIDL interface contract and explains how VDevice_AVBuffer’s AVBuffer service implementation maps to that contract. It is intended to help developers understand what is guaranteed by the AIDL API versus what is currently implemented in VDevice_AVBuffer.

The authoritative contract is the AIDL itself. The HALIF markdown provides additional requirements, recommendations, and example sequences.

## Upstream references (AIDL and HALIF docs)

The upstream AIDL definitions in this workspace are:

- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBufferSpaceListener.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/Pool.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/PoolMetrics.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/HeapMetrics.aidl`

The upstream HALIF documentation in this workspace is:

- `rdk-halif-aidl-17/docs/halif/av_buffer/current/av_buffer.md`

The user-provided upstream links correspond to the same content:

- https://github.com/rdkcentral/rdk-halif-aidl/tree/develop/avbuffer/current/com/rdk/hal/avbuffer
- https://github.com/rdkcentral/rdk-halif-aidl/blob/develop/docs/halif/av_buffer/current/av_buffer.md

## VDevice_AVBuffer implementation entry points (vDevice repository)

The vDevice repository implementation that serves the AIDL API is:

- Service process entrypoint: `../src/service/vcomponent_BufferService.cpp`
- AIDL service implementation class: `../src/aidl/vcomponent_AvBufferManager.cpp`
- Service header: `../include/avbuffer/vcomponent_AvBufferManager.h`

The internal heap/pool helpers are:

- Heap: `../include/avbuffer/vcomponent_HeapHal.h` and `../src/utility/vcomponent_HeapHal.cpp`
- Pool + allocation tracking: `../include/avbuffer/vcomponent_PoolHal.h` and `../src/utility/vcomponent_PoolHal.cpp`

## AIDL surface summary

### Service name and invalid handle constant

From `IAVBuffer.aidl`:

- `const String serviceName = "AVBuffer";`
- `const long INVALID_HANDLE = -1;`

VDevice_AVBuffer mapping:

- `AvBufferManager::getServiceName()` returns `IAVBuffer::serviceName().c_str()`, which is `"AVBuffer"`.

### Primary types (parcelables and callback)

From upstream AIDL:

- `Pool` is a parcelable containing `byte handle`, with `Pool.INVALID_POOL = -1`.
- `HeapMetrics` contains `boolean secure`, `int bytesUsed`, `int bytesTotal`.
- `PoolMetrics` contains `Pool poolHandle`, `int bytesUsed`, `int bytesTotal`.
- `IAVBufferSpaceListener` is a `oneway` interface with `onSpaceAvailable()`.

VDevice_AVBuffer mapping:

- The service uses the AIDL-generated C++ classes in headers included from the build output (for example: `com/rdk/hal/avbuffer/BnAVBuffer.h`, `com/rdk/hal/avbuffer/IAVBuffer.h`, and `com/rdk/hal/avbuffer/IAVBufferSpaceListener.h` as included by `vcomponent_AvBufferManager.h`).
- `PoolImpl` inherits from the AIDL `Pool` parcelable type (see `class PoolImpl : public AvBufferPool` where `AvBufferPool` is an alias of `::com::rdk::hal::avbuffer::Pool`).

## Method-by-method mapping

This table summarizes the AIDL contract and the observed VDevice_AVBuffer behavior (based on the current implementation code). “Observed behavior” describes what the implementation does today, including partial or stubbed features.

| AIDL method | Contract summary (from AIDL / HALIF) | VDevice_AVBuffer behavior (current code) |
|---|---|---|
| `HeapMetrics getHeapMetrics(boolean secureHeap)` | Returns bytesUsed/bytesTotal for the selected heap. HALIF allows reporting 0 bytes if a heap is not implemented. | Implemented. Returns `bytesUsed` as the sum of pool sizes created in the heap (not sum of allocations) and `bytesTotal` from `HeapImpl::GetSize()`. If heap is missing/unavailable, returns zeros without error. |
| `Pool createVideoPool(boolean secureHeap, IVideoDecoder.Id, IAVBufferSpaceListener)` | Creates a pool from secure or non-secure heap. Invalid decoder id must raise `EX_ILLEGAL_ARGUMENT`. Out of memory must raise `EX_SERVICE_SPECIFIC` with `HALError::OUT_OF_MEMORY`. Returns `Pool.INVALID_POOL` on failure. | Partially implemented. If `secureHeap==true`, it returns `EX_ILLEGAL_ARGUMENT` (secure heap is not supported in this implementation path). Non-secure pools are created from a shared-memory heap (`HeapImpl`). Pool handle selection follows the recommended 0..127 scheme and is globally unique across heaps in the service lifetime. Listener is required (null listener yields `EX_ILLEGAL_ARGUMENT`). |
| `Pool createAudioPool(boolean secureHeap, IAudioDecoder.Id, IAVBufferSpaceListener)` | Similar to `createVideoPool`. Allows `IAudioDecoder.Id.UNDEFINED` for “system audio” pools. | Partially implemented. If `secureHeap==true`, it returns `EX_ILLEGAL_ARGUMENT`. For non-secure: validates listener, accepts `UNDEFINED`, otherwise uses range validation against the configured decoder-count. Uses the same pool-handle scheme as video pools. |
| `boolean destroyPool(Pool)` | Returns true if pool handle is valid and pool is destroyed. If pool has outstanding allocations, must raise `EX_SERVICE_SPECIFIC` with `HALError::NOT_EMPTY`. | Implemented for non-secure and (conditionally) secure if compiled with `SECURE_SUPPORTED`. It returns `false` (no exception) if the handle is invalid or unknown. If alloc list is not empty, it returns service-specific error `HALError::NOT_EMPTY`. It also clears pending “space available” requests for the pool and detaches the listener. |
| `PoolMetrics getPoolMetrics(Pool)` | Invalid pool handle must raise `EX_ILLEGAL_ARGUMENT`. bytesUsed is “total bytes used by allocations created inside the pool”. | Implemented. Invalid or unknown pool handle raises `EX_ILLEGAL_ARGUMENT`. bytesTotal is the pool size; bytesUsed is the sum of client-requested allocation sizes (`AllocInfo::size`), not the sum of internal allocated sizes (`AllocInfo::allocatedSize`). |
| `PoolMetrics[] getAllPoolMetrics(boolean secureHeap)` | Returns metrics for all pools in the selected heap. | Implemented, but the current code selects the heap using `HeapImpl* heap = _secureHeap ? _secureHeap : _nonSecureHeap;` and does not use the `secureHeap` parameter. This means the returned heap’s metrics depend on whether `_secureHeap` exists, not on the requested heap type. |
| `long alloc(Pool, int size)` | Returns a globally-unique handle on success. Returns `INVALID_HANDLE` if pool handle is invalid or size > pool size. If OOM, throws service-specific `HALError::OUT_OF_MEMORY`. | Implemented with vendor policy. Invalid pool handle returns `INVALID_HANDLE`. Invalid size (<=0) returns `INVALID_HANDLE`. OOM is returned as `EX_SERVICE_SPECIFIC` with `HALError::OUT_OF_MEMORY`. The allocation handle encodes the pool id in the top byte (poolId << 56) plus a monotonically increasing sequence number. Padding (for video) is supported (64 bytes padding is set on video pools). |
| `boolean free(long bufferHandle)` | Frees a previously allocated handle. Returns false if invalid. | Implemented for the non-secure heap only (the code explicitly sets `bSecure=false`). It searches all pools in the non-secure heap for a matching allocation handle, and removes it if found. If not found, it returns false. |
| `boolean notifyWhenSpaceAvailable(Pool, int size)` | After OOM, client may register for callback when enough space is available for an allocation of `size`. Callback is `IAVBufferSpaceListener.onSpaceAvailable()` on the listener passed during pool creation. | Implemented with an internal pending-request set per pool. The implementation attempts to trigger callbacks immediately if space is already available and also triggers after `trimSize()` (and can be triggered after frees depending on call sites). It avoids invoking binder callbacks while holding the global lock. |
| `boolean trimSize(long bufferHandle, int newSize)` | Only the last allocated block may be trimmed; otherwise must raise `EX_ILLEGAL_STATE`. Trimming reduces allocation size. | Partially implemented. VDevice_AVBuffer implements trimming as a best-effort update of `AllocInfo::size` and `AllocInfo::allocatedSize`, but it does not enforce the “must be last allocation” rule and does not raise `EX_ILLEGAL_STATE` in the current code path. After trimming, the implementation re-evaluates pending notify requests and may fire callbacks. |
| `boolean isValid(long bufferHandle)` | Returns whether the handle is valid. | Implemented by scanning all pools in both heaps (if present). |
| `long[] getAllocList(Pool)` | Debug API. Invalid pool must raise `EX_ILLEGAL_ARGUMENT`. | Implemented: returns list of outstanding allocation handles in the pool. |
| `byte[] calculateSHA1(long bufferHandle)` | Optional. If unimplemented, must raise `EX_UNSUPPORTED_OPERATION`. | Unimplemented by design. Always returns `EX_UNSUPPORTED_OPERATION`. |

## Handle uniqueness requirements versus VDevice_AVBuffer

HALIF requirements (from `av_buffer.md`) include:

- Pool handles must be globally unique across heaps and clients and should be positive int8 values (0..127). Immediate reuse is discouraged.
- Buffer handles must be globally unique across pools and clients.

VDevice_AVBuffer status:

- Pool handles are allocated in the positive range 0..127 and checked for uniqueness across both heaps (`_nonSecureHeap` and `_secureHeap`), which is aligned with HALIF recommendations.
- Allocation handles are generated from a service-local monotonically increasing counter and encode the pool id into the upper bits, which strongly supports global uniqueness during the service lifetime.

## Secure heap support (current status)

HALIF expects both secure and non-secure heap APIs to be supported, but allows a heap to report 0 bytes if not implemented.

VDevice_AVBuffer status:

- `HeapImpl` supports only non-secure shared memory. Constructing a secure heap logs “NOT SUPPORTED” and leaves the heap address as null.
- `createVideoPool(true, ...)` and `createAudioPool(true, ...)` currently reject secure pool creation and return `EX_ILLEGAL_ARGUMENT`.

In other words, the AIDL method signatures exist and compile, but secure-heap operation is not enabled in the default VDevice_AVBuffer flow.

## Configuration: heap sizing and “DecoderID”

VDevice_AVBuffer uses an HFP YAML file parsed via `ut_kvp` to configure:

- `avbuffer/nonSecureHeapBytes`
- `avbuffer/secureHeapBytes`
- `avbuffer/DecoderID` (used in VDevice_AVBuffer as a decoder-count parameter for pool sizing and decoder-id validation)

The configuration is stored in:

- `../vcomponent_configurations/hfp-avbuffer.yaml`

The service entrypoint (`vcomponent_BufferService.cpp`) requires the path to this YAML file as its first CLI argument and stores it into `AvBufferManager::_avbufferHFPPath` before publishing the binder service.

## Notes on error signaling

The AIDL contract uses a mixture of:

- “Return-value errors” (for example returning `INVALID_HANDLE` or `false`), and
- “Binder exceptions” (for example `EX_ILLEGAL_ARGUMENT`, `EX_SERVICE_SPECIFIC` with HALError codes).

VDevice_AVBuffer generally follows this pattern, but there are cases where behavior differs from the contract’s intent. For example, pool allocation “size > pool size” is described in AIDL as a return of `INVALID_HANDLE` without exception, but VDevice_AVBuffer’s `alloc()` currently returns `EX_SERVICE_SPECIFIC` in that case.

This is important for client code because it impacts whether an error must be handled via return-code checks or exception/status inspection.

## Sources

This document is based on:

- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBufferSpaceListener.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/Pool.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/PoolMetrics.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/HeapMetrics.aidl`
- `rdk-halif-aidl-17/docs/halif/av_buffer/current/av_buffer.md`
- `../include/avbuffer/vcomponent_AvBufferManager.h`
- `../src/aidl/vcomponent_AvBufferManager.cpp`
- `../src/service/vcomponent_BufferService.cpp`
- `../include/avbuffer/vcomponent_HeapHal.h`
- `../src/utility/vcomponent_HeapHal.cpp`
- `../include/avbuffer/vcomponent_PoolHal.h`
- `../src/utility/vcomponent_PoolHal.cpp`
- `../vcomponent_configurations/hfp-avbuffer.yaml`
