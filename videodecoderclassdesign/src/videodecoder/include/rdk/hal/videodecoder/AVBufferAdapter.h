/*
 * AVBuffer adapter layer for the Video Decoder class design stub.
 *
 * This file models the relationship between video decoder APIs and the AV Buffer
 * concepts used by the RDK HALIF AIDL interfaces.
 *
 * IMPORTANT: Ownership & free rules (modeled for the class design)
 * ---------------------------------------------------------------
 * - AVBufferHandle values are opaque IDs (typically allocated by IAVBuffer.alloc()).
 * - "Taking ownership" of a handle means the receiver is responsible for freeing it
 *   exactly once (directly or indirectly) and the sender MUST NOT free it afterwards.
 * - "Borrowed" handle usage means the sender retains responsibility for freeing it;
 *   the receiver may read/map/copy but MUST NOT free it.
 *
 * In the production stack, freeing is done through the AV Buffer service:
 *   com.rdk.hal.avbuffer.IAVBuffer.free(handle)
 * and mapping is done via libavbufferhelper's mapHandle()/unmapHandle().
 *
 * This stub layer provides:
 * - IAVBufferAdapter: a narrow interface for "free" plus optional map/unmap hooks
 * - OwnedAVBufferHandle: RAII wrapper that enforces "free exactly once"
 * - comments that document expected ownership transfer at call boundaries
 */

#pragma once

#include "rdk/hal/videodecoder/types.h"

#include <cstdint>
#include <utility>

namespace rdk::hal::videodecoder
{
/**
 * Adapter interface to the AV Buffer subsystem.
 *
 * Real implementations would bridge to:
 * - AV Buffer service binder proxy (IAVBuffer.free)
 * - libavbufferhelper for map/unmap/copy operations (non-secure handles)
 *
 * The VideoDecoder class design uses this adapter to:
 * - document which component owns a buffer handle at each step
 * - ensure buffers are freed exactly once when owned by the decoder/controller
 */
class IAVBufferAdapter
{
public:
    virtual ~IAVBufferAdapter() = default;

    // PUBLIC_INTERFACE
    virtual bool freeHandle(AVBufferHandle handle) noexcept = 0;
    /**
     * Free the given AVBufferHandle.
     *
     * Ownership rule:
     * - Caller MUST own the handle (or otherwise guarantee it will not be freed elsewhere).
     * - The adapter MUST treat this as a final free; handle becomes invalid after success.
     */

    // Optional helper operations (no-op by default in stub implementations).
    // These exist for completeness but are not required by the videodecoder stub logic.
    // PUBLIC_INTERFACE
    virtual void* mapHandle(AVBufferHandle /*handle*/, uint32_t* /*outSize*/) noexcept { return nullptr; }
    /** Map a non-secure handle into the process. Borrowed use: does NOT transfer ownership. */

    // PUBLIC_INTERFACE
    virtual bool unmapHandle(AVBufferHandle /*handle*/) noexcept { return false; }
    /** Unmap a previously mapped handle. Borrowed use: does NOT transfer ownership. */
};

/**
 * RAII wrapper that models "handle ownership transfer" and "free exactly once".
 *
 * Design intent:
 * - Use OwnedAVBufferHandle whenever the decoder/controller takes ownership
 *   of an AVBufferHandle parameter (i.e., after a successful enqueue/decode call).
 * - On destruction, it will call adapter->freeHandle(handle) if still owned.
 *
 * This wrapper is move-only to prevent accidental double-free.
 */
class OwnedAVBufferHandle
{
public:
    OwnedAVBufferHandle() = default;

    explicit OwnedAVBufferHandle(IAVBufferAdapter* adapter, AVBufferHandle handle) noexcept
        : m_adapter(adapter), m_handle(handle)
    {
    }

    ~OwnedAVBufferHandle() { reset(); }

    OwnedAVBufferHandle(const OwnedAVBufferHandle&) = delete;
    OwnedAVBufferHandle& operator=(const OwnedAVBufferHandle&) = delete;

    OwnedAVBufferHandle(OwnedAVBufferHandle&& other) noexcept
        : m_adapter(other.m_adapter), m_handle(other.m_handle)
    {
        other.m_adapter = nullptr;
        other.m_handle = 0;
    }

    OwnedAVBufferHandle& operator=(OwnedAVBufferHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_adapter = other.m_adapter;
            m_handle = other.m_handle;
            other.m_adapter = nullptr;
            other.m_handle = 0;
        }
        return *this;
    }

    // PUBLIC_INTERFACE
    bool isValid() const noexcept { return m_handle != 0; }
    /** Returns true if this wrapper currently owns a non-zero handle. */

    // PUBLIC_INTERFACE
    AVBufferHandle get() const noexcept { return m_handle; }
    /** Get the owned handle without transferring ownership. */

    // PUBLIC_INTERFACE
    AVBufferHandle release() noexcept
    {
        /**
         * Transfer ownership out of this RAII wrapper to the caller.
         * After release(), the wrapper will no longer free the handle.
         */
        AVBufferHandle tmp = m_handle;
        m_handle = 0;
        m_adapter = nullptr;
        return tmp;
    }

    // PUBLIC_INTERFACE
    void reset() noexcept
    {
        /**
         * Free the currently owned handle (if any) and clear ownership.
         * Safe to call multiple times; only frees once.
         */
        if (m_handle != 0)
        {
            if (m_adapter)
            {
                (void)m_adapter->freeHandle(m_handle);
            }
            m_handle = 0;
            m_adapter = nullptr;
        }
    }

private:
    IAVBufferAdapter* m_adapter{nullptr}; // non-owning pointer; adapter lifetime managed externally
    AVBufferHandle m_handle{0};           // 0 means "no owned handle"
};

} // namespace rdk::hal::videodecoder
