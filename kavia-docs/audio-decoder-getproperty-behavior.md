<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/Other/audio-decoder-getproperty-behavior.md
- Operation: write
- Timestamp: 2026-02-12T08:14:53.493827+00:00
- Restored At: 2026-02-23T05:04:11.252259+00:00
- Task ID: cm219d4578
-->

# Audio Decoder getProperty() Behavior (TEVDevice vComponent)

## Scope

This document summarizes the behavior of the Audio Decoder `getProperty()` AIDL API as implemented by the TEVDevice vComponent `vcomponent::audiodecoder::VcomponentAudioDecoder`, focusing on the supported property keys, returned types and default values, and how runtime/read-only properties are tracked and synthesized.

The behavior described here is taken directly from the current implementation in `10002556%2FTEVDevice`.

## API overview

The AIDL method implemented is:

`getProperty(::com::rdk::hal::audiodecoder::Property property, std::optional<::com::rdk::hal::PropertyValue>* _aidl_return)`

The implementation follows these core rules:

1. If the return pointer is null, the method returns a Binder exception status (`EX_NULL_POINTER`).
2. For unknown or unsupported property keys, the method returns `std::nullopt` in `_aidl_return` and returns `Status::ok()` (no exception).
3. For supported property keys, the method returns a `PropertyValue` whose union tag matches the required AIDL type. In this implementation, values are returned using either:
   1. `PropertyValue::Value::Tag::intValue` for integer properties, or
   2. `PropertyValue::Value::Tag::stringValue` for string properties.

## Supported properties

### RESOURCE_ID

`Property::RESOURCE_ID` is always supported.

The vComponent seeds this stable property at construction time and also synthesizes it directly in `getProperty()`:

1. Returned type is an integer `PropertyValue` (`intValue`).
2. Returned value is `m_id.value`.

### Runtime-tracked properties (synthesized fields)

Several properties are treated as runtime properties and are not primarily read from the `m_properties` map. Instead, they are tracked in explicit member fields and synthesized in `getProperty()`.

This aligns with the comment in the header that all properties can be read in any state, and that some properties are runtime-synthesized.

#### LOW_LATENCY_MODE

`Property::LOW_LATENCY_MODE` is supported.

1. Returned type is integer (`intValue`).
2. Returned value is `m_lowLatencyMode`.
3. Default value is `0`.

#### AV_SOURCE

`Property::AV_SOURCE` is supported.

1. Returned type is integer (`intValue`).
2. Returned value is `m_avSource`.
3. Default value is `0`, which the code comments indicate corresponds to `AVSource::UNKNOWN`.

#### SECURE_AUDIO (read-only, synthesized as integer 0/1)

`Property::SECURE_AUDIO` is supported and is treated as read-only.

1. Returned type is integer (`intValue`), not a boolean union.
2. Returned value is `1` when `m_secureAudio` is `true`, otherwise `0`.
3. Default value is `0` because `m_secureAudio` defaults to `false`.

The `m_secureAudio` value is updated during `open()` to reflect the `secure` parameter when the open succeeds.

### AC-4 override properties (defaulted values)

The implementation explicitly treats AC-4 override properties as out-of-scope for this vComponent, but still returns stable defaults so clients can behave predictably.

The following keys are supported with these default return values:

1. `Property::AC4_PRESENTATION_GROUP_INDEX`
   1. Type: integer (`intValue`)
   2. Default value: `-1`
2. `Property::AC4_PREFERRED_LANG1`
   1. Type: string (`stringValue`)
   2. Default value: `""` (empty string)
3. `Property::AC4_PREFERRED_LANG2`
   1. Type: string (`stringValue`)
   2. Default value: `""` (empty string)
4. `Property::AC4_ASSOCIATED_TYPE`
   1. Type: integer (`intValue`)
   2. Default value: `-1`
5. `Property::AC4_AUTO_SELECTION_PRIORITY`
   1. Type: integer (`intValue`)
   2. Default value: `-1`
6. `Property::AC4_MIXER_BALANCE`
   1. Type: integer (`intValue`)
   2. Default value: `255`
7. `Property::AC4_ASSOCIATED_AUDIO_MIXING_ENABLE`
   1. Type: integer (`intValue`)
   2. Default value: `-1`

### Metrics properties (default -1 when not implemented)

The implementation explicitly supports the metrics properties below and returns `-1` for each. This matches the stated intent in code comments that these metrics return `-1` when not implemented by the vendor.

1. `Property::METRIC_FRAMES_DECODED`
   1. Type: integer (`intValue`)
   2. Default value: `-1`
2. `Property::METRIC_DECODE_ERRORS`
   1. Type: integer (`intValue`)
   2. Default value: `-1`
3. `Property::METRIC_FRAMES_DROPPED`
   1. Type: integer (`intValue`)
   2. Default value: `-1`

## Unknown keys and dynamic/future properties

After the explicit `switch(property)` handling, the implementation falls back to a dynamic property map:

`std::unordered_map<int32_t, ::com::rdk::hal::PropertyValue> m_properties;`

The behavior is:

1. If the property is not present in `m_properties`, `_aidl_return` is set to `std::nullopt` and the call returns `Status::ok()`.
2. If present, the stored `PropertyValue` is returned.

In the current implementation, the map is only seeded with `RESOURCE_ID` during construction, and most behavior is driven by explicit runtime fields and switch cases. However, the map fallback provides a forward-compatible mechanism for future properties to be stored and served without changing the switch logic.

## Runtime tracking and secure mode interaction with open()

Although this document is focused on `getProperty()`, secure mode is tracked in a way that directly affects the `SECURE_AUDIO` property.

The `open(codec, secure, listener, _aidl_return)` implementation enforces the secure capability contract:

1. If `secure == true` is requested and `m_capabilities.supportsSecure` is `false`, then `open()` returns a null controller (`*_aidl_return = nullptr`) and returns `Status::ok()`. In this case, the decoder state and `m_secureAudio` are not updated.
2. If secure is allowed (either `secure == false` or capability supports secure), `open()` returns a new controller instance, sets:
   1. `m_secureAudio = secure`
   2. `m_state = State::READY`

As a result, after a successful open, `getProperty(SECURE_AUDIO)` will report `1` if and only if the decoder was opened with `secure == true`.

## Concurrency and state

The implementation uses a mutex (`m_lock`) to protect:

1. `m_state`
2. `m_properties`
3. runtime property fields (`m_lowLatencyMode`, `m_avSource`, `m_secureAudio`)
4. `m_eventListener`

`getProperty()` locks this mutex for the duration of evaluation and return-value construction.

The `getProperty()` implementation itself does not enforce state gating. It is intended to allow reading properties independent of the current decoder state.
