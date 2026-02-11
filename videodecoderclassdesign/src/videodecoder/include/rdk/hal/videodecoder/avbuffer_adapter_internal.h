/*
 * Internal header for the videodecoderclassdesign stub.
 *
 * This provides access to a process-wide stub AV buffer adapter instance.
 * Not intended as part of any public product API; used only by the skeleton
 * sources to demonstrate ownership transfer and free rules.
 */

#pragma once

#include "rdk/hal/videodecoder/AVBufferAdapter.h"

namespace rdk::hal::videodecoder
{
// PUBLIC_INTERFACE
IAVBufferAdapter* getStubAVBufferAdapter();
/** Returns a process-wide stub AV buffer adapter instance (class design stub only). */
} // namespace rdk::hal::videodecoder
