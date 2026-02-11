/*
 * VideoDecoderController stub skeleton.
 *
 * Aligns with AIDL:
 *   com.rdk.hal.videodecoder.IVideoDecoderController
 */

#pragma once

#include "rdk/hal/videodecoder/listeners.h"
#include "rdk/hal/videodecoder/types.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace rdk::hal::videodecoder
{
/**
 * Design-time controller object returned by VideoDecoder::open().
 *
 * Responsibilities:
 * - start/stop decode flow
 * - accept encoded buffers by AVBuffer handle
 * - flush / EOS / discontinuity signaling
 * - accept codec-specific data
 */
class VideoDecoderController
{
public:
    /**
     * Construct a controller for a given codec/security mode.
     *
     * The listener is used for controller callbacks (frame output, errors).
     */
    VideoDecoderController(Codec codec, bool secure,
                           std::shared_ptr<IVideoDecoderControllerListener> listener);

    ~VideoDecoderController();

    VideoDecoderController(const VideoDecoderController&) = delete;
    VideoDecoderController& operator=(const VideoDecoderController&) = delete;

    // PUBLIC_INTERFACE
    void start();
    /**
     * Starts the Video Decoder (AIDL: start()).
     */

    // PUBLIC_INTERFACE
    void stop();
    /**
     * Stops the Video Decoder (AIDL: stop()).
     */

    // PUBLIC_INTERFACE
    bool setProperty(Property property, const PropertyValue& propertyValue);
    /**
     * Sets a controller property (AIDL: setProperty()).
     */

    // PUBLIC_INTERFACE
    bool decodeBuffer(Nanoseconds nsPresentationTime, AVBufferHandle bufferHandle);
    /**
     * Submit an encoded frame buffer (AIDL: decodeBuffer()).
     */

    // PUBLIC_INTERFACE
    void flush(bool reset);
    /**
     * Flushes queued buffers and optionally resets internal state (AIDL: flush()).
     */

    // PUBLIC_INTERFACE
    void signalDiscontinuity();
    /**
     * Signals PTS discontinuity (AIDL: signalDiscontinuity()).
     */

    // PUBLIC_INTERFACE
    void signalEOS();
    /**
     * Signals end-of-stream (AIDL: signalEOS()).
     */

    // PUBLIC_INTERFACE
    bool parseCodecSpecificData(CSDVideoFormat csdVideoFormat, const std::vector<uint8_t>& codecData);
    /**
     * Provide codec specific data (AIDL: parseCodecSpecificData()).
     */

    // PUBLIC_INTERFACE
    State getInternalState() const;
    /** Returns the internal controller state (design helper; not in AIDL). */

private:
    void notifyErrorLocked(int32_t errorCode, const char* message);

    Codec m_codec{Codec::UNKNOWN};
    bool m_secure{false};
    std::shared_ptr<IVideoDecoderControllerListener> m_listener;

    mutable std::mutex m_mutex{};
    State m_state{State::READY};

    // Stored codec specific data (CSD) as provided by the client.
    std::vector<uint8_t> m_csd{};
};

} // namespace rdk::hal::videodecoder
