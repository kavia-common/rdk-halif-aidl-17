#include "rdk/hal/videodecoder/VideoDecoder.h"

#include <mutex>

namespace rdk::hal::videodecoder
{
namespace
{
// NOTE: The AIDL contract states:
// - getCapabilities() can be called at any time
// - returned value must not change between calls
//
// So we return a process-static, const-in-practice value.
// If/when this is backed by a real platform HAL, populate these fields from
// platform queries during initialization and keep them immutable thereafter.
Capabilities buildStaticCapabilities()
{
    Capabilities caps{};

    // Repo mapping note:
    // - AIDL Capabilities parcelable fields are:
    //     CodecCapabilities[] supportedCodecs;
    //     DynamicRange[] supportedDynamicRanges;
    //     boolean supportsSecure;
    //
    // This repo's current C++ "class design" stub Capabilities (types.h) is
    // different and only models supported codecs + operational modes.
    //
    // Therefore, we map the AIDL concept to the closest existing C++ types in
    // this repository (Codec + OperationalMode).
    caps.supportedCodecs = {Codec::AVC, Codec::HEVC, Codec::AV1};
    caps.supportedModes = {OperationalMode::NON_TUNNELLED};

    return caps;
}

const Capabilities& staticCapabilities()
{
    static const Capabilities kCaps = buildStaticCapabilities();
    return kCaps;
}
} // namespace

/**
 * VComponent-style entrypoint for VideoDecoder capabilities.
 *
 * This file is intended to host the HAL "vcomponent" surface methods for the
 * VideoDecoder component. The AIDL source of truth is:
 *   com.rdk.hal.videodecoder.IVideoDecoder.getCapabilities()
 *
 * AIDL signature:
 *   Capabilities getCapabilities();
 *
 * AIDL behavioral requirements:
 * - Callable at any time (no state dependency).
 * - Return value is not allowed to change between calls.
 *
 * This implementation satisfies the immutability requirement by returning a
 * static capability set.
 */

// PUBLIC_INTERFACE
Capabilities getCapabilities()
{
    /** Returns the capabilities of the Video Decoder as defined by the AIDL contract. */
    return staticCapabilities();
}

} // namespace rdk::hal::videodecoder
