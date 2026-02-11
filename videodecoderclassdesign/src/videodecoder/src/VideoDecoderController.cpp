#include "rdk/hal/videodecoder/VideoDecoderController.h"

#include <stdexcept>

namespace rdk::hal::videodecoder
{
VideoDecoderController::VideoDecoderController(
    Codec codec,
    bool secure,
    std::shared_ptr<IVideoDecoderControllerListener> listener)
    : m_codec(codec), m_secure(secure), m_listener(std::move(listener))
{
    // In AIDL, the controller exists once open() succeeds and the decoder reaches READY.
    m_state = State::READY;
}

VideoDecoderController::~VideoDecoderController()
{
    // Stub: no resources.
}

// PUBLIC_INTERFACE
void VideoDecoderController::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // AIDL precondition: decoder must be READY before start.
    if (m_state != State::READY)
    {
        notifyErrorLocked(/*errorCode*/ -1, "start() called in invalid state");
        return;
    }

    // Stub state transition: READY -> STARTED (skip STARTING for simplicity in stub).
    m_state = State::STARTED;
}

// PUBLIC_INTERFACE
void VideoDecoderController::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // AIDL precondition: must be STARTED.
    if (m_state != State::STARTED)
    {
        notifyErrorLocked(/*errorCode*/ -1, "stop() called in invalid state");
        return;
    }

    // Stub state transition: STARTED -> READY.
    m_state = State::READY;
}

// PUBLIC_INTERFACE
bool VideoDecoderController::setProperty(Property /*property*/, const PropertyValue& /*propertyValue*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Stub: no properties supported yet.
    return false;
}

// PUBLIC_INTERFACE
bool VideoDecoderController::decodeBuffer(Nanoseconds /*nsPresentationTime*/, AVBufferHandle /*bufferHandle*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // AIDL precondition: must be STARTED.
    if (m_state != State::STARTED)
    {
        notifyErrorLocked(/*errorCode*/ -1, "decodeBuffer() called in invalid state");
        return false;
    }

    // Stub behavior: accept buffer but do nothing with it.
    // A real implementation would validate AVBuffer handle, map it, feed HW decoder,
    // and then release the buffer back to AVBuffer.
    return true;
}

// PUBLIC_INTERFACE
void VideoDecoderController::flush(bool /*reset*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // AIDL precondition: must be STARTED.
    if (m_state != State::STARTED)
    {
        notifyErrorLocked(/*errorCode*/ -1, "flush() called in invalid state");
        return;
    }

    // Stub: no buffered data tracked.
}

// PUBLIC_INTERFACE
void VideoDecoderController::signalDiscontinuity()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // AIDL precondition: must be STARTED.
    if (m_state != State::STARTED)
    {
        notifyErrorLocked(/*errorCode*/ -1, "signalDiscontinuity() called in invalid state");
        return;
    }

    // Stub: no timeline management.
}

// PUBLIC_INTERFACE
void VideoDecoderController::signalEOS()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // AIDL precondition: must be STARTED.
    if (m_state != State::STARTED)
    {
        notifyErrorLocked(/*errorCode*/ -1, "signalEOS() called in invalid state");
        return;
    }

    // Stub: immediately notify EOS frame output if listener exists.
    if (m_listener)
    {
        FrameMetadata md{};
        md.endOfStream = true;
        m_listener->onFrameOutput(md);
    }
}

// PUBLIC_INTERFACE
bool VideoDecoderController::parseCodecSpecificData(CSDVideoFormat /*csdVideoFormat*/, const std::vector<uint8_t>& codecData)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // AIDL precondition: must be STARTED.
    if (m_state != State::STARTED)
    {
        notifyErrorLocked(/*errorCode*/ -1, "parseCodecSpecificData() called in invalid state");
        return false;
    }

    if (codecData.empty())
    {
        notifyErrorLocked(/*errorCode*/ -1, "parseCodecSpecificData() called with empty codecData");
        return false;
    }

    m_csd = codecData;
    return true;
}

// PUBLIC_INTERFACE
State VideoDecoderController::getInternalState() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void VideoDecoderController::notifyErrorLocked(int32_t errorCode, const char* message)
{
    // Called with lock held.
    if (m_listener)
    {
        m_listener->onError(errorCode, message ? message : "");
    }
}

} // namespace rdk::hal::videodecoder
