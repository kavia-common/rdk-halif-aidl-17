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
#include <unordered_map>
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
    std::vector<OperationalMode> getSupportedOperationalModes() const;
    /** AIDL: getSupportedOperationalModes() */

    // PUBLIC_INTERFACE
    std::shared_ptr<VideoDecoder> getVideoDecoder(const VideoDecoderId& videoDecoderId);
    /** AIDL: getVideoDecoder(id) returns nullable IVideoDecoder */

private:
    void ensureInitializedLocked();

    mutable std::mutex m_mutex{};
    bool m_initialized{false};

    std::vector<OperationalMode> m_supportedModes{};
    std::unordered_map<int32_t, std::shared_ptr<VideoDecoder>> m_decoders{};
};

} // namespace rdk::hal::videodecoder
