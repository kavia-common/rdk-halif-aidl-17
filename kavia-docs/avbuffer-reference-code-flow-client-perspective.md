<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/Other/avbuffer-reference-code-flow-client-perspective.md
- Operation: write
- Timestamp: 2026-01-21T19:02:08.913648+00:00
- Restored At: 2026-02-23T05:04:11.242621+00:00
- Task ID: cm219d4578
-->

# AVBuffer Reference Implementation Code Flow (Client Perspective)

## Purpose and scope

This document explains, end-to-end, what happens in the AVBuffer reference implementation when a client uses the `IAVBuffer` service. It is written from the client’s perspective but names the concrete service-side functions and files to read so a developer can quickly jump into code.

This document covers the non-secure shared-memory heap initialization, pool creation (audio/video), allocation and free behavior, metrics, pool destruction, binder death handling (local vs remote binder), and the out-of-band (OOB) IPC path used by the AVBufferHelper helper library.

## High-level flow (client → service)

At runtime, the client interacts with the binder service `AVBuffer` (AIDL interface `IAVBuffer`). The typical flow is:

1. The AVBuffer service process starts and registers itself with the binder Service Manager.
2. A client calls `getService("AVBuffer")` to obtain an `sp<IAVBuffer>`.
3. The client creates pools: `createVideoPool(...)` and/or `createAudioPool(...)`, passing a per-pool `IAVBufferSpaceListener`.
4. The client allocates buffers from a pool using `alloc(poolHandle, size)` and receives a 64-bit buffer handle.
5. For non-secure handles, the client maps/unmaps the handle using the helper library (AVBufferHelper) which talks to the service over a Unix domain socket to translate handle → heap offset and allocation size.
6. When the client is finished, it frees buffers via `free(handle)` and destroys pools via `destroyPool(poolHandle)`.
7. If the client process dies without destroying pools, the service attempts to clean up pools via binder death notifications (when available).

## Service startup and heap initialization (non-secure shared memory)

### What the service does at startup

The service main is extremely small:

- File: `10002556%2FTEVDevice/Reference_Code/virtualdevice-rdk-media-hal-develop/avbuffer/service/RDKAVBufferService.cpp`
- Entry: `main()`
- Calls: `RDKAVBuffer::publishAndJoinThreadPool()`

This publishes the binder service with name `IAVBuffer::serviceName()` (AIDL constant: `"AVBuffer"`) and joins the binder thread pool.

### What the service constructs and why it matters

The binder service object constructor is where heap and IPC infrastructure is created:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Class: `RDKAVBuffer`
- Function: `RDKAVBuffer::RDKAVBuffer()`

Key actions:

1. It creates the non-secure heap:
   - `HeapImpl(Heap::HEAP_NON_SECURE, NON_SECURE_HEAP_SIZE)`
2. It creates the secure heap object too, but secure heap size is currently `0` and secure heap is not supported in this reference:
   - `HeapImpl(Heap::HEAP_SECURE, SECURE_HEAP_SIZE)`
3. It creates and initializes an OOB IPC server for AVBufferHelper:
   - `_avbIPC = new AVBufferHALIPC(this);`
   - `_avbIPC->init();`

### Non-secure heap: shared memory specifics

The non-secure heap is backed by POSIX shared memory:

- File: `.../avbuffer/avbuffer/heap_hal.h`, `.../avbuffer/avbuffer/heap_hal.cpp`
- Constant:
  - `AVBUFFER_SHARED_MEMORY_NAME = "AVBufferSharedMemory"`
  - `AVBUFFER_SHARED_MEMORY_SIZE = 27 * 1024 * 1024` (27 MiB)

When shared memory mode is enabled (`static bool s_bUseSharedMemory = true`), `HeapImpl` does:

1. `shm_open(AVBUFFER_SHARED_MEMORY_NAME, O_CREAT | O_EXCL | O_RDWR, 0666)`
2. `ftruncate(fd, size)`
3. `mmap(..., MAP_SHARED, fd, 0)`

This creates a single global heap mapping in the service process. Clients do not directly receive a pointer; they receive handles that can be mapped via the helper library using offset translation.

## Pool creation (audio vs video)

### Client call sequence

A client creates pools using:

- `createVideoPool(secureHeap=false, videoDecoderId, listener) -> Pool`
- `createAudioPool(secureHeap=false, audioDecoderId, listener) -> Pool`

Example usage exists in:

- File: `.../avbuffer/test/RDKAVBufferTest.cpp` (binder-based test)
- File: `.../avbuffer/test/avbuffer_aidl_client.cpp` (AIDL client sample)

Both tests:
1. Call `getService(IAVBuffer::serviceName())`
2. Start a binder thread pool in the client (`ProcessState::self()->startThreadPool()`) so callbacks can be delivered
3. Call `createVideoPool()` and `createAudioPool()` with non-null listeners

### How pool size and placement is chosen

On the service side:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.h`
  - `POOL_VIDEO_SIZE = 25 * 1024 * 1024` (25 MiB)
  - `POOL_AUDIO_SIZE = 2 * 1024 * 1024` (2 MiB)
  - `NON_SECURE_HEAP_SIZE = AVBUFFER_SHARED_MEMORY_SIZE` (27 MiB)

Pool creation logic:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Function: `RDKAVBuffer::createPool(bool secureHeap, bool bAudio, uint8_t nPoolID, listener)`

The service:
1. Selects heap (`_nonSecureHeap` when `secureHeap=false`).
2. Requests space in the heap:
   - `heap->FindFreeSpace(poolSize, offset)`
3. Creates a `PoolImpl(offset, poolSize, heap->GetHeapAddr(), nPoolID)`
4. Attaches the listener (`pool->SetListener(listener)`) and adds the pool to heap bookkeeping (`heap->AddPool(pool)`).

Because the non-secure heap is 27 MiB, the reference configuration is essentially intended to hold one 25 MiB video pool plus one 2 MiB audio pool (with minimal slack).

### Video padding behavior (important client-facing consequence)

After creating the video pool, the service sets a padding requirement:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Function: `createVideoPool(...)`

It calls:
- `pool->SetPadding(64)`

This means that allocations from a video pool use:
- `allocSize = requestedSize + 64`, then aligned
- but the client-visible requested size stored in allocation metadata remains the original `requestedSize`

Audio pool creation does not set this padding, so allocations behave as:
- `allocSize = requestedSize`, then aligned

The comment explains the rationale: FFmpeg decoding APIs require 64 bytes of input padding.

## Allocation and free (what a client should expect)

### Allocation: `IAVBuffer.alloc(poolHandle, size)`

Service implementation:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Function: `Status RDKAVBuffer::alloc(const Pool& poolHandle, int32_t size, int64_t* outHandle)`

The service:
1. Validates inputs (`poolHandle.handle != INVALID_HANDLE` and `size > 0`).
2. Determines which pool implementation to use (`getPool(poolHandle)`).
3. Two distinct paths exist:
   1. If the pool is a “frame allocator” pool (vendor-internal frame pools), it allocates from `BufferManager` instead of `PoolImpl`.
   2. Otherwise, it allocates from the heap-backed `PoolImpl` region.

For heap-backed pools (the main client case in this reference), the flow is:

1. Read pool padding:
   - `padding = pool->GetPadding()`
2. Compute `allocSize = requestedSize + padding`
3. Find free space in the pool:
   - `pool->FindFreeSpace(allocSize, offset)`
   - Note: `FindFreeSpace` also adjusts `allocSize` to meet alignment requirements.
4. Generate a unique 64-bit allocation handle:
   - `nAllocID = _handleGenerator.GetUniqueAllocID(pool->GetHandle())`
5. Create allocation record:
   - `AllocInfo(offset, requestedSize, allocSize, ..., nAllocID)`
6. Add allocation to pool list:
   - `pool->AddAllocInfo(allocInfo)`
7. Return `nAllocID` to the client.

From the client’s perspective:
- The returned handle is the only stable identifier for the allocation.
- The actual allocated bytes can be larger than requested due to padding and alignment (especially for video).

### Free: `IAVBuffer.free(bufferHandle)`

Service implementation:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Function: `Status RDKAVBuffer::free(int64_t bufferHandle, bool* outOk)`

The service:
1. Extracts `poolID` from the handle.
2. Routes to either:
   - frame-pool free (via `BufferManager`) if the handle belongs to a frame allocator pool, or
   - heap-backed free by locating `PoolImpl` and then the `AllocInfo`.
3. Removes allocation record:
   - `pool->RemoveAllocInfo(allocInfo)` (which deletes the `AllocInfo`)

From the client’s perspective:
- After `free()`, the handle should be treated as invalid.
- Free order is not required to be FIFO, but fragmentation is possible (pool code searches gaps).

### Trim: `IAVBuffer.trimSize(handle, newSize)`

Service implementation supports trimming only the last allocated handle from a pool:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Function: `Status RDKAVBuffer::trimSize(int64_t bufferHandle, int32_t newSize, bool* outOk)`

It enforces:
- the allocation must be the most recent one in that pool (`allocList.back()->handle == bufferHandle`), otherwise `EX_ILLEGAL_STATE`.

This is important for client design because it is not a general “realloc”; it is a last-allocation-only optimization.

## Metrics (heap and pool)

### Heap metrics: `getHeapMetrics(secureHeap)`

Service implementation:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Function: `Status RDKAVBuffer::getHeapMetrics(bool secureHeap, HeapMetrics* out)`

In this reference:
- Secure heap metrics are mostly placeholder (secure heap not supported).
- For non-secure heap, `bytesUsed` is computed as the sum of pool sizes currently created in the heap (not sum of allocations). This means bytesUsed increases on pool creation and decreases on pool destruction, but does not reflect allocation/free churn within a pool.

Client implication:
- Treat heap `bytesUsed` as “capacity carved into pools”, not “allocated bytes”.

### Pool metrics: `getPoolMetrics(poolHandle)`

Service implementation:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Function: `Status RDKAVBuffer::getPoolMetrics(const Pool& poolHandle, PoolMetrics* out)`

It computes:
- `bytesTotal = pool->GetSize()`
- `bytesUsed = sum(allocInfo->size)` across allocations

Client implication:
- Pool `bytesUsed` reflects requested sizes, not padding/alignment overhead (`allocatedSize` is tracked separately in `AllocInfo` but not reported in metrics).

## Pool destruction and lifecycle rules

### Normal destruction: `destroyPool(poolHandle)`

Service implementation:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Function: `Status RDKAVBuffer::destroyPool(const Pool& poolHandle, bool* outOk)`

Behavior:
- For non-secure heap pools, the reference implementation refuses to destroy if there are outstanding allocations:
  - It checks `pool->GetAllocList().size()`
  - If allocations remain, it returns a service-specific error (currently mapped to `HALError::OUT_OF_MEMORY` in this reference code path, although the interface spec expects `NOT_EMPTY`).
- If empty, it unlinks death recipient tracking, removes pool from heap (`RemovePool`), and deletes pool.

Client implication:
- The client should treat “destroyPool failed” as a strong signal it leaked buffers; it must free all handles from that pool first.

### What happens on service shutdown

`RDKAVBuffer::~RDKAVBuffer()` deletes heaps and IPC objects. `HeapImpl::~HeapImpl()` iterates through pools and deletes them. `PoolImpl::~PoolImpl()` logs leaks if allocations remain and tries to clean them up.

This means that a service restart may clean up leaked pools/allocations, but clients should not rely on this.

## Binder death handling (listener death) and “local vs remote” behavior

### What the service tracks

When a client creates a pool, it must provide an `IAVBufferSpaceListener`. The service uses that listener’s binder identity to set up cleanup:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- In: `createVideoPool(...)` and `createAudioPool(...)`

It does:
1. `clientBinder = IInterface::asBinder(listener)`
2. `clientBinder->linkToDeath(this)`
3. Tracks pool ownership:
   - `_clientPools[binderPtr].push_back(nPoolID)`

On pool destruction, it tries to:
- `unlinkToDeath(this)`
- remove that poolID from `_clientPools`

### binderDied callback

If the listener binder dies, the service receives:

- File: `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
- Function: `void RDKAVBuffer::binderDied(const wp<IBinder>& binder)`

It:
1. Looks up the dead client binder pointer in `_clientPools`.
2. For each pool owned by that client, force-destroys the pool without checking allocation count.
3. Removes the client entry from `_clientPools`.

Client implication:
- If a client process crashes, the service attempts to prevent leaking entire pools indefinitely, by deleting the pools it associates with that client’s listener binder.

### Local vs remote binder (why linkToDeath may be benignly failing)

In some execution modes (notably single-process or “local binder” scenarios), `linkToDeath` can return `INVALID_OPERATION`, because death notifications are only meaningful for remote binder objects.

The repository includes an explicit note in the client sample:

- File: `.../avbuffer/test/avbuffer_aidl_client.cpp`
- Function: `isBenignLinkToDeathStatus(status_t st)`

Client implication:
- If your test uses a local binder object (or runs in an environment where binder treats the interface as local), `linkToDeath` failure can be non-fatal. In true multi-process usage, you typically expect `linkToDeath` to work.

## Helper library and OOB IPC (mapping handles to memory)

### Why OOB exists

Clients need to map non-secure buffers into their own process. The service holds the heap mapping; clients need offset/size metadata to map the shared memory region correctly.

This reference uses a Unix domain socket server started by the service to answer queries from helper code.

### IPC server details

- Files:
  - `.../avbuffer/avbuffer/avbuffer_hal_ipc.h`
  - `.../avbuffer/avbuffer/avbuffer_hal_ipc.cpp`

Key details:
- Socket path: `"/tmp/avbufferhelper"`
- Listener thread accepts connections and spawns per-connection threads.
- Supports messages:
  - `GetHandleOffset` → `ReturnHandleOffset`
  - `GetHandleSize` → `ReturnHandleSize`
  - `AllocateFrame` / `FreeFrame` (frame pool support)

When a helper asks for an offset:
- It calls `RDKAVBuffer::getHandleOffset(handle)` which finds the pool and the `AllocInfo` and returns `AllocInfo::offset`.

When a helper asks for size:
- It calls `RDKAVBuffer::getHandleAllocationSize(handle)` which returns either:
  - frame allocation size from `BufferManager`, or
  - `AllocInfo::size` for heap-backed allocations.

Client implication:
- For non-secure heap-backed allocations, mapping is based on `(poolOffset + allocOffset)` into the shared heap mapping. The helper library is the supported client entry point for this translation, not direct service internals.

## Key files and functions to read (in recommended order)

### Client-facing API surface (AIDL)

1. `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
2. `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/Pool.aidl`
3. `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/HeapMetrics.aidl`
4. `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/PoolMetrics.aidl`

### Service entry and main binder implementation

1. `.../avbuffer/service/RDKAVBufferService.cpp` (`main`, publish)
2. `.../avbuffer/avbuffer/RDKAVBuffer.h` (service class + constants)
3. `.../avbuffer/avbuffer/RDKAVBuffer.cpp`
   - `RDKAVBuffer::RDKAVBuffer()` (heap + IPC init)
   - `createVideoPool`, `createAudioPool`, `destroyPool`
   - `alloc`, `free`, `trimSize`
   - `getHeapMetrics`, `getPoolMetrics`
   - `binderDied`

### Heap and pool internals (allocation algorithm)

1. `.../avbuffer/avbuffer/heap_hal.h` / `heap_hal.cpp`
   - `HeapImpl::FindFreeSpace`
   - shared memory setup
2. `.../avbuffer/avbuffer/pool_hal.h` / `pool_hal.cpp`
   - `PoolImpl::FindFreeSpace`
   - `PoolImpl::AddAllocInfo`, `RemoveAllocInfo`
   - padding and alignment behavior

### Helper IPC

1. `.../avbuffer/avbuffer/avbuffer_hal_ipc.h` / `avbuffer_hal_ipc.cpp`
   - socket protocol and request handling
   - uses `getHandleOffset` and `getHandleAllocationSize`

### Concrete examples (how clients invoke it)

1. `.../avbuffer/test/RDKAVBufferTest.cpp`
2. `.../avbuffer/test/avbuffer_aidl_client.cpp`

## Practical client lifecycle checklist

A client that wants predictable behavior should:

1. Acquire the service via Service Manager and start a client binder thread pool if it expects callbacks.
2. Create pools with non-null listeners, and keep those listener objects alive until the pool is destroyed.
3. Allocate buffers using `alloc()`.
4. For non-secure handles, use the helper library to map/unmap and write data.
5. Free all buffer handles before calling `destroyPool()`.
6. Handle `destroyPool()` failures as “allocations still outstanding” and clean up leaked handles.
7. Treat `linkToDeath` warnings as environment-dependent: they may be benign in local-binder scenarios, but important in true multi-process usage.
