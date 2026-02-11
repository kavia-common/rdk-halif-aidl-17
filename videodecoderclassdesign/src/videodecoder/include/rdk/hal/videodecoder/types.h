/*
 * Stub types for the Video Decoder class design.
 *
 * This is NOT part of the AIDL interfaces. It exists to let the design skeleton
 * compile standalone, while keeping naming aligned with the AIDL definitions.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rdk::hal::videodecoder
{
/**
 * Mirrors `com.rdk.hal.videodecoder.IVideoDecoder.Id`.
 */
struct VideoDecoderId
{
    static constexpr int32_t UNDEFINED = -1;
    int32_t value{UNDEFINED};

    bool isValid() const { return value != UNDEFINED; }
};

/**
 * Minimal stand-in for `com.rdk.hal.State`.
 *
 * AIDL state names are shared across modules; here we model only the common
 * decoder lifecycle states referenced by the VideoDecoder AIDL docs.
 */
enum class State
{
    CLOSED,
    OPENING,
    READY,
    STARTING,
    STARTED,
    STOPPING,
    CLOSING
};

/**
 * Minimal stand-in for `com.rdk.hal.videodecoder.OperationalMode`.
 */
enum class OperationalMode
{
    TUNNELLED,
    NON_TUNNELLED,
    GRAPHICS_TEXTURE
};

/**
 * Minimal stand-in for `com.rdk.hal.videodecoder.Codec`.
 *
 * Real enum values should match AIDL when integrated with generated types.
 */
enum class Codec
{
    AVC,
    HEVC,
    AV1,
    VP9,
    UNKNOWN
};

/**
 * Minimal stand-in for `com.rdk.hal.videodecoder.CSDVideoFormat`.
 */
enum class CSDVideoFormat
{
    AVC_DECODER_CONFIGURATION_RECORD,
    HEVC_DECODER_CONFIGURATION_RECORD,
    AV1_DECODER_CONFIGURATION_RECORD
};

/**
 * Minimal stand-in for `com.rdk.hal.videodecoder.Property`.
 */
enum class Property
{
    UNKNOWN = 0
};

/**
 * Minimal stand-in for `com.rdk.hal.PropertyValue`.
 *
 * In the real implementation this is a structured union-like parcelable.
 */
struct PropertyValue
{
    std::string value;
};

/**
 * Minimal stand-in for `com.rdk.hal.videodecoder.PropertyKVPair`.
 */
struct PropertyKVPair
{
    Property property{Property::UNKNOWN};
    std::optional<PropertyValue> propertyValue;
};

/**
 * Minimal stand-in for `com.rdk.hal.videodecoder.Capabilities`.
 */
struct Capabilities
{
    std::vector<Codec> supportedCodecs;
    std::vector<OperationalMode> supportedModes;
};

/**
 * Minimal stand-in for `com.rdk.hal.videodecoder.FrameMetadata`.
 *
 * We include only the EOS flag referenced by `signalEOS()` documentation.
 */
struct FrameMetadata
{
    bool endOfStream{false};
};

/**
 * AVBuffer handle as used by AIDL (`long bufferHandle`).
 *
 * In AIDL, `long` is signed 64-bit; here we treat handles as opaque 64-bit.
 */
using AVBufferHandle = uint64_t;

/**
 * Nanoseconds presentation time as used by AIDL (`long nsPresentationTime`).
 */
using Nanoseconds = int64_t;

} // namespace rdk::hal::videodecoder
