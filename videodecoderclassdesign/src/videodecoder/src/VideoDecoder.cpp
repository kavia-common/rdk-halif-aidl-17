#include "rdk/hal/videodecoder/VideoDecoder.h"

#include <algorithm>

namespace rdk::hal::videodecoder
{
VideoDecoder::VideoDecoder(VideoDecoderId id) : m_id(id)
{
    // Stub: a decoder starts CLOSED as per AIDL lifecycle.
    m_state = State::CLOSED;
}

VideoDecoder::~VideoDecoder()
{
    // Stub: no resources.
}

// PUBLIC_INTERFACE
Capabilities VideoDecoder::getCapabilities() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Stub static capabilities.
    Capabilities caps{};
    caps.supportedCodecs = {Codec::AVC, Codec::HEVC, Codec::AV1};
    caps.supportedModes = {OperationalMode::NON_TUNNELLED};
    return caps;
}

// PUBLIC_INTERFACE
std::optional<PropertyValue> VideoDecoder::getProperty(Property /*property*/) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Stub: no properties supported.
    return std::nullopt;
}

// PUBLIC_INTERFACE
bool VideoDecoder::getPropertyMulti(const std::vector<Property>& properties, std::vector<PropertyKVPair>& outPropertyKVList) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (properties.empty())
    {
        return false;
    }

    outPropertyKVList.clear();
    outPropertyKVList.reserve(properties.size());

    // Stub: return keys with null values.
    for (auto p : properties)
    {
        PropertyKVPair kv{};
        kv.property = p;
        kv.propertyValue = std::nullopt;
        outPropertyKVList.push_back(std::move(kv));
    }

    return true;
}

// PUBLIC_INTERFACE
State VideoDecoder::getState() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

// PUBLIC_INTERFACE
std::shared_ptr<VideoDecoderController> VideoDecoder::open(
    Codec codec,
    bool secure,
    std::shared_ptr<IVideoDecoderControllerListener> videoDecoderControllerListener)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // AIDL precondition: must be CLOSED to open.
    if (m_state != State::CLOSED)
    {
        return nullptr;
    }

    if (!videoDecoderControllerListener)
    {
        // AIDL would throw EX_NULL_POINTER; stub returns null to indicate failure.
        return nullptr;
    }

    setStateLocked(State::OPENING);

    // Stub: immediate transition to READY and create controller.
    m_controller = std::make_shared<VideoDecoderController>(codec, secure, std::move(videoDecoderControllerListener));
    setStateLocked(State::READY);

    return m_controller;
}

// PUBLIC_INTERFACE
bool VideoDecoder::close(const std::shared_ptr<VideoDecoderController>& videoDecoderController)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // AIDL precondition: must be READY to close.
    if (m_state != State::READY)
    {
        return false;
    }

    if (!videoDecoderController || videoDecoderController != m_controller)
    {
        return false;
    }

    setStateLocked(State::CLOSING);

    // Stub: drop controller and transition to CLOSED.
    m_controller.reset();
    setStateLocked(State::CLOSED);
    return true;
}

// PUBLIC_INTERFACE
bool VideoDecoder::registerEventListener(std::shared_ptr<IVideoDecoderEventListener> videoDecoderEventListener)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!videoDecoderEventListener)
    {
        return false;
    }

    auto it = std::find(m_eventListeners.begin(), m_eventListeners.end(), videoDecoderEventListener);
    if (it != m_eventListeners.end())
    {
        // AIDL: cannot register twice.
        return false;
    }

    m_eventListeners.push_back(std::move(videoDecoderEventListener));
    return true;
}

// PUBLIC_INTERFACE
bool VideoDecoder::unregisterEventListener(std::shared_ptr<IVideoDecoderEventListener> videoDecoderEventListener)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!videoDecoderEventListener)
    {
        return false;
    }

    auto it = std::find(m_eventListeners.begin(), m_eventListeners.end(), videoDecoderEventListener);
    if (it == m_eventListeners.end())
    {
        return false;
    }

    m_eventListeners.erase(it);
    return true;
}

// PUBLIC_INTERFACE
VideoDecoderId VideoDecoder::getId() const
{
    return m_id;
}

void VideoDecoder::setStateLocked(State newState)
{
    State previous = m_state;
    m_state = newState;
    notifyStateChangedLocked(previous, newState);
}

void VideoDecoder::notifyStateChangedLocked(State previous, State current)
{
    // Called with lock held.
    for (const auto& listener : m_eventListeners)
    {
        if (listener)
        {
            listener->onStateChanged(previous, current);
        }
    }
}

} // namespace rdk::hal::videodecoder
