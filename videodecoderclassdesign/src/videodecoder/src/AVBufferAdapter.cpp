#include "rdk/hal/videodecoder/avbuffer_adapter_internal.h"

namespace rdk::hal::videodecoder
{
/**
 * Minimal stub adapter used by the class design skeleton.
 *
 * In this repository's class design stub build, we cannot call into the actual
 * AV Buffer service or helper library; this implementation therefore models the
 * call boundary and always "succeeds" freeing non-zero handles.
 *
 * This allows unit-style reasoning about ownership transfer without introducing
 * real platform dependencies.
 */
class StubAVBufferAdapter final : public IAVBufferAdapter
{
public:
    bool freeHandle(AVBufferHandle handle) noexcept override
    {
        // In a real implementation this would call IAVBuffer.free(handle).
        // Stub semantics:
        // - freeHandle(0) is treated as a no-op failure (invalid handle).
        // - freeHandle(non-zero) returns true to model successful release.
        return handle != 0;
    }
};

// Factory for internal use by the class design stub.
// Kept in this TU to avoid exposing more public API surface than needed.
IAVBufferAdapter* getStubAVBufferAdapter()
{
    static StubAVBufferAdapter s_adapter{};
    return &s_adapter;
}

} // namespace rdk::hal::videodecoder
