#include "rdk/hal/videodecoder/VideoDecoder.h"

#include <cstdint>

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

    // AIDL Capabilities fields:
    //   CodecCapabilities[] supportedCodecs;
    //   DynamicRange[] supportedDynamicRanges;
    //   boolean supportsSecure;
    //
    // Populate with a stable, representative set for this stub implementation.
    // When integrating with a real backend, replace these literals with platform
    // queries performed once at init time.
    caps.supportsSecure = false;

    // Per-codec capability entries. Profile/Level are left UNKNOWN in this stub
    // (the AIDL enums exist, but this repository's standalone stub does not
    // define full sets yet).
    CodecCapabilities avc{};
    avc.codec = Codec::AVC;
    avc.profile = CodecProfile::UNKNOWN;
    avc.level = CodecLevel::UNKNOWN;
    avc.maxFrameRate = 60;
    avc.maxFrameWidth = 3840;
    avc.maxFrameHeight = 2160;

    CodecCapabilities hevc{};
    hevc.codec = Codec::HEVC;
    hevc.profile = CodecProfile::UNKNOWN;
    hevc.level = CodecLevel::UNKNOWN;
    hevc.maxFrameRate = 60;
    hevc.maxFrameWidth = 3840;
    hevc.maxFrameHeight = 2160;

    CodecCapabilities av1{};
    av1.codec = Codec::AV1;
    av1.profile = CodecProfile::UNKNOWN;
    av1.level = CodecLevel::UNKNOWN;
    av1.maxFrameRate = 60;
    av1.maxFrameWidth = 3840;
    av1.maxFrameHeight = 2160;

    caps.supportedCodecs = {avc, hevc, av1};

    // Dynamic range support (representative defaults).
    // If the platform supports additional ranges (e.g., Dolby Vision), extend
    // this list accordingly.
    caps.supportedDynamicRanges = {DynamicRange::SDR, DynamicRange::HDR10, DynamicRange::HLG};

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
