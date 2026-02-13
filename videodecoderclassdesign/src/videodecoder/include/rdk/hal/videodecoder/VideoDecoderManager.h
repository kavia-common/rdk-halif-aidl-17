/*
 * VideoDecoderManager stub skeleton.
 *
 * Aligns with AIDL:
 *   com.rdk.hal.videodecoder.IVideoDecoderManager
 */

#pragma once

#include "rdk/hal/videodecoder/VideoDecoder.h"
#include "rdk/hal/videodecoder/types.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace rdk::hal::videodecoder
{
/**
 * Manager for VideoDecoder resources.
 *
 * Responsibilities:
 * - enumerate available decoder IDs
 * - expose supported operational modes
 * - return decoder objects by ID
 */
class VideoDecoderManager
{
public:
    VideoDecoderManager();
    ~VideoDecoderManager();

    VideoDecoderManager(const VideoDecoderManager&) = delete;
    VideoDecoderManager& operator=(const VideoDecoderManager&) = delete;

    // PUBLIC_INTERFACE
    std::vector<VideoDecoderId> getVideoDecoderIds() const;
    /** AIDL: getVideoDecoderIds() */

    // PUBLIC_INTERFACE
    void getVideoDecoderId(std::vector<VideoDecoderId*>& outIds);
    /**
     * Fill the caller-provided vector with pointers to newly allocated VideoDecoderId objects.
     *
     * Thread-safety:
     * - This method takes the manager mutex.
     * - It performs lazy initialization under the same lock via ensureInitializedLocked().
     *
     * Ownership:
     * - The caller owns the allocated VideoDecoderId objects and must delete them.
     *
     * This API is intended to match environments where the caller provides storage for
     * multiple decoder IDs via pointers.
     */

    // PUBLIC_INTERFACE
    std::vector<OperationalMode> getSupportedOperationalModes() const;
    /** AIDL: getSupportedOperationalModes() */

    // PUBLIC_INTERFACE
    std::shared_ptr<VideoDecoder> getVideoDecoder(const VideoDecoderId& videoDecoderId);
    /** AIDL: getVideoDecoder(id) returns nullable IVideoDecoder */

private:
    void ensureInitializedLocked();

    using DecoderEntry = std::pair<VideoDecoderId, std::shared_ptr<VideoDecoder>>;

    mutable std::mutex m_mutex{};
    bool m_initialized{false};

    std::vector<OperationalMode> m_supportedModes{};
    std::vector<DecoderEntry> m_videoDecoders{};
};

} // namespace rdk::hal::videodecoder
