#include "rdk/hal/videodecoder/VideoDecoderManager.h"

namespace rdk::hal::videodecoder
{
VideoDecoderManager::VideoDecoderManager() = default;

VideoDecoderManager::~VideoDecoderManager() = default;

// PUBLIC_INTERFACE
std::vector<VideoDecoderId> VideoDecoderManager::getVideoDecoderIds() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Stub initialization is lazy but safe.
    // const method: we don't mutate in this stub; real implementation might cache.
    std::vector<VideoDecoderId> ids{};
    ids.push_back(VideoDecoderId{0});
    return ids;
}

// PUBLIC_INTERFACE
std::vector<OperationalMode> VideoDecoderManager::getSupportedOperationalModes() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<OperationalMode> modes{};
    modes.push_back(OperationalMode::NON_TUNNELLED);
    return modes;
}

// PUBLIC_INTERFACE
std::shared_ptr<VideoDecoder> VideoDecoderManager::getVideoDecoder(const VideoDecoderId& videoDecoderId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!videoDecoderId.isValid() || videoDecoderId.value < 0)
    {
        return nullptr;
    }

    ensureInitializedLocked();

    for (const auto& [id, decoder] : m_videoDecoders)
    {
        if (id.value == videoDecoderId.value)
        {
            return decoder;
        }
    }

    return nullptr;
}

void VideoDecoderManager::ensureInitializedLocked()
{
    if (m_initialized)
    {
        return;
    }

    // Stub: create a single decoder instance with id=0.
    const VideoDecoderId id0{0};

    // Store (id, decoder) adjacent so callers can easily pass around ids/pointers together.
    m_videoDecoders.emplace_back(id0, std::make_shared<VideoDecoder>(id0));

    m_supportedModes = {OperationalMode::NON_TUNNELLED};

    m_initialized = true;
}

} // namespace rdk::hal::videodecoder
