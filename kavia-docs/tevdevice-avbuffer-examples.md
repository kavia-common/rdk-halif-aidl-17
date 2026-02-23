<!--
MongoDB Document Metadata:
- Original File Path: 10002556%2FTEVDevice/docs/tevdevice-avbuffer-examples.md
- Operation: write
- Timestamp: 2026-02-19T08:52:14.545432+00:00
- Restored At: 2026-02-23T05:04:11.254193+00:00
- Task ID: cm219d4578
-->

# VDevice_AVBuffer Examples

## Overview

This document provides example commands and client-side pseudo-code for interacting with the VDevice_AVBuffer service, which implements the HALIF AIDL interface `com.rdk.hal.avbuffer.IAVBuffer`.

These examples are written to match the vDevice repository’s current implementation and configuration mechanism.

## Example 1: Run the service with an HFP YAML

The service requires the HFP YAML path as an argument. The vDevice repository provides a default example file at:

- `../vcomponent_configurations/hfp-avbuffer.yaml`

From the repository root (after building), run:

```bash
./RDKAVBufferService ./vcomponent_configurations/hfp-avbuffer.yaml
```

The service entrypoint validates that the file exists and is readable, then sets `AvBufferManager::_avbufferHFPPath` and publishes the binder service.

## Example 2: Client pseudo-code (create pool, alloc, write, free, destroy)

This is intentionally pseudo-code because this repository does not include a full client implementation. It illustrates the intended use of the AIDL interface.

```cpp
#include <binder/IServiceManager.h>
#include <com/rdk/hal/avbuffer/IAVBuffer.h>
#include <com/rdk/hal/avbuffer/IAVBufferSpaceListener.h>

using ::android::sp;
using ::android::defaultServiceManager;

sp<com::rdk::hal::avbuffer::IAVBuffer> getAvBuffer()
{
    sp<android::IServiceManager> sm = defaultServiceManager();
    sp<android::IBinder> b = sm->getService(android::String16("AVBuffer"));
    return android::interface_cast<com::rdk::hal::avbuffer::IAVBuffer>(b);
}

class MySpaceListener : public com::rdk::hal::avbuffer::BnAVBufferSpaceListener
{
public:
    android::binder::Status onSpaceAvailable() override
    {
        // Called when notifyWhenSpaceAvailable triggers.
        return android::binder::Status::ok();
    }
};

void example_basic_flow()
{
    sp<com::rdk::hal::avbuffer::IAVBuffer> avb = getAvBuffer();

    sp<MySpaceListener> listener = new MySpaceListener();

    // Decoder IDs are validated by VDevice_AVBuffer using a range check derived from HFP DecoderID.
    com::rdk::hal::videodecoder::IVideoDecoder::Id videoId;
    videoId.value = 0;

    com::rdk::hal::avbuffer::Pool pool = avb->createVideoPool(false, videoId, listener);

    const int frameSize = 4096;
    const long handle = avb->alloc(pool, frameSize);

    if (handle == com::rdk::hal::avbuffer::IAVBuffer::INVALID_HANDLE) {
        // Pool invalid or size invalid (or other non-exception failure).
        return;
    }

    // In the upstream design, a helper library maps handles to process pointers.
    // The vDevice repository does not provide that helper in this repository, but the contract expects it.
    // You would typically do:
    //   mapHandle(handle, &ptr); memcpy(ptr, data, frameSize); unmapHandle(handle, ptr);

    avb->free(handle);

    const bool destroyed = avb->destroyPool(pool);
    (void)destroyed;
}
```

## Example 3: Handling out-of-memory (OOM) with notifyWhenSpaceAvailable

In the AIDL contract, `alloc()` may fail with a service-specific error `HALError::OUT_OF_MEMORY`. VDevice_AVBuffer implements this behavior when the pool cannot satisfy an allocation request.

The client flow is:

1. `alloc(pool, size)` throws service-specific OOM.
2. Client calls `notifyWhenSpaceAvailable(pool, size)`.
3. Client receives `onSpaceAvailable()` callback on the listener passed at pool creation.

Pseudo-code sketch:

```cpp
try {
    long h = avb->alloc(pool, size);
    // use h ...
} catch (const android::binder::Status& st) {
    // In real code you inspect st.exceptionCode() and serviceSpecificErrorCode().
    // If OOM:
    bool ok = avb->notifyWhenSpaceAvailable(pool, size);
    if (!ok) {
        // invalid pool or invalid size
    }
}
```

Important VDevice_AVBuffer note:

- VDevice_AVBuffer stores pending notify requests per pool and may invoke callbacks immediately if space is already available at registration time.
- VDevice_AVBuffer tries to avoid calling callbacks while holding its global lock.

## Example 4: Heap and pool metrics

### Heap metrics

The AIDL call:

- `HeapMetrics getHeapMetrics(boolean secureHeap)`

In VDevice_AVBuffer, `bytesUsed` is computed as the sum of pool sizes created in the heap (not the sum of allocations). This matches the comment in the VDevice_AVBuffer implementation.

Pseudo-code:

```cpp
com::rdk::hal::avbuffer::HeapMetrics m = avb->getHeapMetrics(false);
printf("non-secure heap used=%d total=%d\n", m.bytesUsed, m.bytesTotal);
```

### Pool metrics

The AIDL call:

- `PoolMetrics getPoolMetrics(Pool poolHandle)`

In VDevice_AVBuffer, `bytesUsed` is computed as the sum of requested allocation sizes.

```cpp
com::rdk::hal::avbuffer::PoolMetrics pm = avb->getPoolMetrics(pool);
printf("pool used=%d total=%d\n", pm.bytesUsed, pm.bytesTotal);
```

## Example 5: Destroying pools safely

The AIDL contract requires that all allocations from a pool are freed before destroying the pool. VDevice_AVBuffer enforces this by returning a service-specific error if allocations exist.

A safe shutdown sequence is:

1. free all outstanding `bufferHandle`s.
2. `destroyPool(poolHandle)`.

If the client process dies, VDevice_AVBuffer also performs binder-death cleanup based on the listener binder death notification.

## Troubleshooting tips (vDevice-specific)

### Service fails to start

- Ensure you pass the YAML path argument: `RDKAVBufferService <path-to-hfp-avbuffer.yaml>`.
- Ensure the YAML file is readable; the service checks read access before publishing.
- If required YAML keys are missing or invalid, the current implementation can throw during manager construction.

### Secure pool creation fails

VDevice_AVBuffer currently rejects `secureHeap=true` pool creation and reports an illegal argument exception, because secure heap creation is not supported in the current implementation.

## Sources

This document is based on:

- `../src/service/vcomponent_BufferService.cpp`
- `../src/aidl/vcomponent_AvBufferManager.cpp`
- `../include/avbuffer/vcomponent_AvBufferManager.h`
- `../vcomponent_configurations/hfp-avbuffer.yaml`
- `rdk-halif-aidl-17/avbuffer/current/com/rdk/hal/avbuffer/IAVBuffer.aidl`
- `rdk-halif-aidl-17/docs/halif/av_buffer/current/av_buffer.md`
