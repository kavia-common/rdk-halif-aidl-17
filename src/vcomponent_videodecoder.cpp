#include "rdk/hal/videodecoder/VideoDecoder.h"

#include <array>
#include <cstdint>
#include <vector>

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

    // Per-codec capability entries.
    //
    // IMPORTANT: Keep this stable across calls. Populate once here and return a
    // static instance from getCapabilities().
    //
    // This refactor avoids repeating near-identical blocks for each codec by
    // iterating a small list and applying common defaults in the loop.
    constexpr int32_t kDefaultMaxFrameRate = 60;
    constexpr int32_t kDefaultMaxFrameWidth = 3840;
    constexpr int32_t kDefaultMaxFrameHeight = 2160;

    // Codecs supported by this stub implementation. Extend as needed.
    constexpr std::array<Codec, 3> kSupportedCodecs = {Codec::AVC, Codec::HEVC, Codec::AV1};

    std::vector<CodecCapabilities> codecCaps;
    codecCaps.reserve(kSupportedCodecs.size());

    for (const Codec codec : kSupportedCodecs)
    {
        CodecCapabilities cc{};
        cc.codec = codec;

        // Profile/Level are left UNKNOWN in this stub (the AIDL enums exist, but
        // this repository's standalone stub does not define full sets yet).
        cc.profile = CodecProfile::UNKNOWN;
        cc.level = CodecLevel::UNKNOWN;

        // Default decode limits (representative values).
        // If some codecs require different limits, adjust within this loop using
        // a small codec->values mapping/switch (still avoiding repeated blocks).
        cc.maxFrameRate = kDefaultMaxFrameRate;
        cc.maxFrameWidth = kDefaultMaxFrameWidth;
        cc.maxFrameHeight = kDefaultMaxFrameHeight;

        codecCaps.push_back(cc);
    }

    caps.supportedCodecs = std::move(codecCaps);

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
