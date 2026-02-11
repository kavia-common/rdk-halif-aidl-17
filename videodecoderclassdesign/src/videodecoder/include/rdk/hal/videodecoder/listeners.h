/*
 * Listener interfaces for the Video Decoder design skeleton.
 *
 * These mirror the intent of the AIDL listener interfaces:
 * - IVideoDecoderEventListener
 * - IVideoDecoderControllerListener
 *
 * They are *not* binder interfaces here; they are plain C++ interfaces.
 */

#pragma once

#include "rdk/hal/videodecoder/types.h"

#include <cstdint>
#include <memory>
#include <string>

namespace rdk::hal::videodecoder
{
class VideoDecoderController;

/**
 * Controller callbacks (mirrors IVideoDecoderControllerListener intent).
 */
class IVideoDecoderControllerListener
{
public:
    virtual ~IVideoDecoderControllerListener() = default;

    /**
     * Called when a decoded frame is ready for output.
     *
     * In tunnelled mode, this might signal the renderer path; in non-tunnelled
     * mode it may deliver a decoded-frame buffer handle (not modeled here).
     */
    virtual void onFrameOutput(const FrameMetadata& metadata) = 0;

    /**
     * Called when a controller-level error occurs.
     */
    virtual void onError(int32_t errorCode, const std::string& message) = 0;
};

/**
 * Decoder lifecycle/state callbacks (mirrors IVideoDecoderEventListener intent).
 */
class IVideoDecoderEventListener
{
public:
    virtual ~IVideoDecoderEventListener() = default;

    /**
     * Called when the decoder transitions between states.
     */
    virtual void onStateChanged(State previous, State current) = 0;
};

} // namespace rdk::hal::videodecoder
