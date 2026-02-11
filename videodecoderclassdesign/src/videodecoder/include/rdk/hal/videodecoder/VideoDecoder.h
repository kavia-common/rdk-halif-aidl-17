/*
 * VideoDecoder stub skeleton.
 *
 * Aligns with AIDL:
 *   com.rdk.hal.videodecoder.IVideoDecoder
 */

#pragma once

#include "rdk/hal/videodecoder/VideoDecoderController.h"
#include "rdk/hal/videodecoder/listeners.h"
#include "rdk/hal/videodecoder/types.h"

#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace rdk::hal::videodecoder
{
/**
 * Represents a single Video Decoder resource (identified by VideoDecoderId).
 *
 * Responsibilities:
 * - expose capabilities & properties
 * - manage open/close lifecycle and state transitions
 * - manage event listener registration
 * - create controller instances
 */
class VideoDecoder
{
public:
    explicit VideoDecoder(VideoDecoderId id);

    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // PUBLIC_INTERFACE
    Capabilities getCapabilities() const;
    /** AIDL: getCapabilities() */

    // PUBLIC_INTERFACE
    std::optional<PropertyValue> getProperty(Property property) const;
    /** AIDL: getProperty() returns nullable PropertyValue */

    // PUBLIC_INTERFACE
    bool getPropertyMulti(const std::vector<Property>& properties, std::vector<PropertyKVPair>& outPropertyKVList) const;
    /** AIDL: getPropertyMulti() */

    // PUBLIC_INTERFACE
    State getState() const;
    /** AIDL: getState() */

    // PUBLIC_INTERFACE
    std::shared_ptr<VideoDecoderController> open(
        Codec codec,
        bool secure,
        std::shared_ptr<IVideoDecoderControllerListener> videoDecoderControllerListener);
    /** AIDL: open() returns nullable IVideoDecoderController */

    // PUBLIC_INTERFACE
    bool close(const std::shared_ptr<VideoDecoderController>& videoDecoderController);
    /** AIDL: close(controller) */

    // PUBLIC_INTERFACE
    bool registerEventListener(std::shared_ptr<IVideoDecoderEventListener> videoDecoderEventListener);
    /** AIDL: registerEventListener() */

    // PUBLIC_INTERFACE
    bool unregisterEventListener(std::shared_ptr<IVideoDecoderEventListener> videoDecoderEventListener);
    /** AIDL: unregisterEventListener() */

    // PUBLIC_INTERFACE
    VideoDecoderId getId() const;
    /** Returns the resource id (design helper; AIDL encodes it as parcelable). */

private:
    void setStateLocked(State newState);
    void notifyStateChangedLocked(State previous, State current);

    VideoDecoderId m_id{};

    mutable std::mutex m_mutex{};
    State m_state{State::CLOSED};

    // In a real binder implementation, this would be binder identity rather than shared_ptr identity.
    std::vector<std::shared_ptr<IVideoDecoderEventListener>> m_eventListeners{};

    // Current open controller (single-open model as per AIDL doc preconditions).
    std::shared_ptr<VideoDecoderController> m_controller{};
};

} // namespace rdk::hal::videodecoder
