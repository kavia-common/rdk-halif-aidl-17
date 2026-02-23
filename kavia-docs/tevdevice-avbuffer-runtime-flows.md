<!--
MongoDB Document Metadata:
- Original File Path: 10002556%2FTEVDevice/docs/tevdevice-avbuffer-runtime-flows.md
- Operation: write
- Timestamp: 2026-02-19T08:51:59.148260+00:00
- Restored At: 2026-02-23T05:04:11.254126+00:00
- Task ID: cm219d4578
-->

# VDevice_AVBuffer Runtime Flows

## Overview

This document describes runtime behavior of VDevice_AVBuffer’s AVBuffer service in terms of:

- sequence diagrams (control flow),
- data-flow diagrams (buffer/handle lifecycle), and
- notes on how these flows relate to the HALIF AVBuffer AIDL contract.

The upstream AVBuffer contract and reference behavior are defined by:

- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- `rdk-halif-aidl-17/docs/halif/av_buffer/current/av_buffer.md`

The VDevice_AVBuffer implementation is defined by:

- `../src/service/vcomponent_BufferService.cpp`
- `../src/aidl/vcomponent_AvBufferManager.cpp`
- heap/pool helpers in `../src/utility/`

## Startup and service publication

### Sequence: startup, configuration, binder publish

```mermaid
sequenceDiagram
  participant Sys as "systemd or launcher"
  participant Proc as "RDKAVBufferService<br/>vcomponent_BufferService.cpp"
  participant Mgr as "AvBufferManager<br/>BnAVBuffer"
  participant Heap as "HeapImpl (non-secure)"
  participant SM as "Service Manager"

  Sys->>Proc: "exec RDKAVBufferService <hfp-avbuffer.yaml>"
  Proc->>Proc: "validate argv[1] readable"
  Proc->>Proc: "AvBufferManager::_avbufferHFPPath = argv[1]"
  Proc->>Mgr: "publishAndJoinThreadPool()"
  Mgr->>Mgr: "constructor"
  Mgr->>Mgr: "loadHfpConfigNoThrow()"
  Mgr->>Heap: "HeapImpl(HEAP_NON_SECURE, nonSecureHeapBytes)"
  Heap->>Heap: "shm_open + ftruncate + mmap"
  Mgr->>SM: "publish service name 'AVBuffer'"
  Proc->>Proc: "join binder thread pool (blocks)"
```

### Notes

The service name is derived from the upstream AIDL constant `IAVBuffer.serviceName` which is `"AVBuffer"`. VDevice_AVBuffer uses this string via `IAVBuffer::serviceName()` in `AvBufferManager::getServiceName()`.

## Pool lifecycle

### Sequence: create a non-secure pool

In VDevice_AVBuffer’s current implementation, pool creation is supported only for the non-secure heap. Requests for `secureHeap=true` return an illegal argument exception.

```mermaid
sequenceDiagram
  participant C as "Client"
  participant SM as "Service Manager"
  participant Mgr as "IAVBuffer (AvBufferManager)"
  participant Heap as "HeapImpl"
  participant Pool as "PoolImpl"

  C->>SM: "getService('AVBuffer')"
  SM-->>C: "IAVBuffer binder proxy"

  C->>Mgr: "createVideoPool(false, videoDecoderId, listener)"
  Mgr->>Mgr: "validate decoderId in [0..DecoderID-1]"
  Mgr->>Mgr: "validate listener != null"
  Mgr->>Heap: "FindFreeSpace(requestedPoolSize, offset)"
  Heap-->>Mgr: "offset found"
  Mgr->>Pool: "new PoolImpl(offset, poolSize, heapAddr, poolId)"
  Mgr->>Pool: "SetListener(listener)"
  Mgr->>Heap: "AddPool(pool)"
  Mgr-->>C: "Pool{handle = poolId}"
```

### VDevice_AVBuffer pool sizing policy

HALIF states that pool sizes are vendor-defined. VDevice_AVBuffer uses a simple policy:

- `requestedPoolSize = heapSize / DecoderID` (where `DecoderID` is read from HFP YAML and treated as a decoder-count parameter).

## Buffer lifecycle

### Data flow: handle-to-memory mapping in VDevice_AVBuffer

VDevice_AVBuffer’s heap is an `mmap()`-ed shared memory region, and allocations are represented by handles. Internally:

- the allocation record (`AllocInfo`) stores `offset` (pool-relative), `size` and `allocatedSize`,
- the pool computes `ptr = heapAddr + (poolOffset + allocOffset)`.

The allocation pointer is used within the service process for bookkeeping and validation. The upstream HALIF design expects a helper library (`libavbufferhelper`) for mapping/unmapping non-secure buffers in client processes, but that helper library is not implemented in the vDevice code in this repository.

```mermaid
flowchart TD
  H["IAVBuffer.alloc(pool,size)<br/>returns bufferHandle"] --> A["AvBufferManager alloc()"]
  A --> P["PoolImpl::FindFreeSpace()<br/>chooses allocOffset"]
  P --> I["AllocInfo{offset,size,allocatedSize,handle}"]
  I --> Ptr["AllocInfo.ptr = heapAddr + (poolOffset + allocOffset)"]

  H2["IAVBuffer.free(bufferHandle)"] --> F["AvBufferManager free()"]
  F --> Scan["Scan pools in non-secure heap"]
  Scan --> Found{"FindAllocInfo(handle)?"}
  Found -->|yes| Remove["PoolImpl::RemoveAllocInfo()"]
  Found -->|no| Invalid["return false"]
```

### Sequence: allocation and free

```mermaid
sequenceDiagram
  participant C as "Client"
  participant Mgr as "IAVBuffer (AvBufferManager)"
  participant Pool as "PoolImpl"

  C->>Mgr: "bufferHandle = alloc(poolHandle, size)"
  Mgr->>Mgr: "validate pool handle and size"
  Mgr->>Pool: "FindFreeSpace(allocSizeAligned, offset)"
  Pool-->>Mgr: "offset"
  Mgr->>Pool: "AddAllocInfo(new AllocInfo(..., handle))"
  Mgr-->>C: "bufferHandle (int64)"

  C->>Mgr: "ok = free(bufferHandle)"
  Mgr->>Mgr: "scan pools in non-secure heap"
  Mgr->>Pool: "FindAllocInfo(bufferHandle)"
  Pool-->>Mgr: "AllocInfo or null"
  alt found
    Mgr->>Pool: "RemoveAllocInfo(allocInfo)"
    Mgr-->>C: "true"
  else not found
    Mgr-->>C: "false"
  end
```

## Out-of-memory and notifyWhenSpaceAvailable

### Contract intent (HALIF)

HALIF describes that when `alloc()` fails with out-of-memory, the client can call `notifyWhenSpaceAvailable(pool,size)`. When enough space becomes available to satisfy an allocation of that size, the service should invoke `IAVBufferSpaceListener.onSpaceAvailable()` on the listener that was passed during pool creation.

### VDevice_AVBuffer design

VDevice_AVBuffer stores a per-pool set of pending requested sizes. It attempts to:

- fire the callback immediately if the pool can already satisfy the request, and
- fire callbacks after operations that can create space (for example `trimSize()`, and potentially after frees depending on call sites).

The code avoids calling binder callbacks while holding the global lock.

### Sequence: OOM then notify then callback

```mermaid
sequenceDiagram
  participant C as "Client"
  participant Mgr as "IAVBuffer (AvBufferManager)"
  participant Pool as "PoolImpl"
  participant L as "IAVBufferSpaceListener"

  C->>Mgr: "alloc(pool, size)"
  Mgr-->>C: "EX_SERVICE_SPECIFIC: HALError::OUT_OF_MEMORY"

  C->>Mgr: "notifyWhenSpaceAvailable(pool, size)"
  Mgr->>Pool: "GetListener()"
  Mgr->>Mgr: "record pending request (poolId,size)"
  alt space already available
    Mgr->>L: "onSpaceAvailable()"
    Mgr-->>C: "true"
  else space not available
    Mgr-->>C: "true"
  end

  Note over C,Mgr: "Later: free() or trimSize() makes enough space"
  Mgr->>Mgr: "re-check pending requests"
  Mgr->>L: "onSpaceAvailable()"
```

## Trimming behavior

### Contract intent (AIDL)

The AIDL contract states:

- only the last allocated block from a pool may be trimmed, and
- if a non-last buffer is passed, `EX_ILLEGAL_STATE` should be returned.

### VDevice_AVBuffer behavior

VDevice_AVBuffer updates `AllocInfo::size` and may reduce `AllocInfo::allocatedSize` with alignment and padding constraints, but it does not enforce the “must be last allocation” rule. After trimming, VDevice_AVBuffer re-checks pending notify requests and may fire callbacks.

## Pool destruction

### Sequence: destroyPool

```mermaid
sequenceDiagram
  participant C as "Client"
  participant Mgr as "IAVBuffer (AvBufferManager)"
  participant Heap as "HeapImpl"
  participant Pool as "PoolImpl"

  C->>Mgr: "destroyPool(poolHandle)"
  Mgr->>Mgr: "lookup pool in heaps"
  alt pool not found
    Mgr-->>C: "false"
  else found
    Mgr->>Pool: "GetAllocList()"
    alt allocations outstanding
      Mgr-->>C: "EX_SERVICE_SPECIFIC: HALError::NOT_EMPTY"
    else empty
      Mgr->>Pool: "RemoveListener()"
      Mgr->>Heap: "RemovePool(pool)"
      Mgr->>Mgr: "delete PoolImpl"
      Mgr-->>C: "true"
    end
  end
```

## Binder death cleanup

VDevice_AVBuffer uses binder death notifications to clean up pools when the client process that provided the listener dies unexpectedly. The manager tracks pools by the listener binder pointer and then force-destroys those pools on `binderDied()`.

This cleanup is best-effort and does not enforce the “pool must be empty” AIDL requirement that normally applies to `destroyPool()`.

```mermaid
sequenceDiagram
  participant Binder as "Binder driver"
  participant Mgr as "AvBufferManager"
  participant Heap as "HeapImpl"
  participant Pool as "PoolImpl"

  Binder->>Mgr: "binderDied(listenerBinder)"
  Mgr->>Mgr: "lookup pools owned by binder"
  loop for each poolId
    Mgr->>Heap: "FindPool(poolId)"
    Heap-->>Mgr: "PoolImpl*"
    Mgr->>Heap: "RemovePool(pool)"
    Mgr->>Mgr: "delete PoolImpl"
  end
  Mgr->>Mgr: "erase ownership mapping"
```

## Sources

This document is based on:

- `../src/service/vcomponent_BufferService.cpp`
- `../src/aidl/vcomponent_AvBufferManager.cpp`
- `../include/avbuffer/vcomponent_AvBufferManager.h`
- `../include/avbuffer/vcomponent_HeapHal.h`
- `../src/utility/vcomponent_HeapHal.cpp`
- `../include/avbuffer/vcomponent_PoolHal.h`
- `../src/utility/vcomponent_PoolHal.cpp`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBufferSpaceListener.aidl`
- `rdk-halif-aidl-17/docs/halif/av_buffer/current/av_buffer.md`
