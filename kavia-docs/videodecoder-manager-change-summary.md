<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/Other/videodecoder-manager-change-summary.md
- Operation: write
- Timestamp: 2026-02-13T11:09:07.827340+00:00
- Restored At: 2026-02-23T05:04:11.252477+00:00
- Task ID: cm219d4578
-->

# VideoDecoderManager.h/.cpp Change Summary (Diff-Style)

## Scope and files reviewed

This note summarizes the concrete, observable changes in the `VideoDecoderManager` stub skeleton as currently present in the repository, focusing on the “vector-of-pairs decoder storage” and updated API logic.

Files reviewed:

- `videodecoderclassdesign/src/videodecoder/include/rdk/hal/videodecoder/VideoDecoderManager.h`
- `videodecoderclassdesign/src/videodecoder/src/VideoDecoderManager.cpp`

## Diff-style summary of changes

### Storage for decoders: map to “adjacent id+decoder” container

In the implementation, decoder storage is now modeled as an ordered container that stores `VideoDecoderId` and its corresponding `VideoDecoder` instance adjacent to each other (a vector-of-pairs style container). This is evidenced by:

- The use of `m_videoDecoders.emplace_back(id0, std::make_shared<VideoDecoder>(id0));`
- Iteration over `m_videoDecoders` using structured binding: `for (const auto& [id, decoder] : m_videoDecoders)`

This indicates a conceptual change from an `unordered_map<id, decoder>` approach to a `std::vector<std::pair<VideoDecoderId, std::shared_ptr<VideoDecoder>>>` (or equivalent) approach.

Concrete snippet (from `VideoDecoderManager.cpp`):

```cpp
// Store (id, decoder) adjacent so callers can easily pass around ids/pointers together.
m_videoDecoders.emplace_back(id0, std::make_shared<VideoDecoder>(id0));
```

### Updated `getVideoDecoder()` lookup logic

`getVideoDecoder(const VideoDecoderId&)` now:

1. Validates the ID and returns `nullptr` for invalid / negative values:

```cpp
if (!videoDecoderId.isValid() || videoDecoderId.value < 0)
{
    return nullptr;
}
```

2. Ensures lazy initialization through `ensureInitializedLocked()`.

3. Performs a linear scan over the vector-of-pairs container and matches by comparing numeric `value` fields:

```cpp
for (const auto& [id, decoder] : m_videoDecoders)
{
    if (id.value == videoDecoderId.value)
    {
        return decoder;
    }
}
```

4. Returns `nullptr` when not found.

This is a functional/API behavior change versus a typical map lookup (`find`) that would be expected with an `unordered_map`.

### Lazy initialization helper updated to populate the new container

`ensureInitializedLocked()` now:

- Guards on `m_initialized`
- Creates a single stub decoder with id `0`
- Inserts it into `m_videoDecoders` using `emplace_back(id, decoder)`
- Sets `m_supportedModes` to `{OperationalMode::NON_TUNNELLED}`
- Sets `m_initialized = true`

### Note: header/implementation mismatch (important)

As currently checked in, the header does not match the implementation with respect to decoder storage:

- In `VideoDecoderManager.h`, the member is declared as:

```cpp
std::unordered_map<int32_t, std::shared_ptr<VideoDecoder>> m_decoders{};
```

- In `VideoDecoderManager.cpp`, the code uses a member named `m_videoDecoders` and treats it as a vector-like container (supports `emplace_back` and structured binding over element pairs).

This is not a small stylistic difference; it indicates either:

1. The header has not yet been updated to reflect the new storage approach, or
2. The implementation was updated but member renaming/type changes were not propagated, or
3. There are additional changes expected elsewhere that are not present in the header.

If compiled as-is, this mismatch would typically fail to compile because `m_videoDecoders` is not declared in the class definition.

## Minimal pseudo-diff (illustrative)

This section is intentionally limited to the essence of the observed changes (not a full patch), highlighting the “map-like” vs “vector-of-pairs” nature and the new lookup logic.

```diff
- // old (expected with unordered_map)
- auto it = m_decoders.find(videoDecoderId.value);
- if (it != m_decoders.end()) return it->second;

+ // new (observed in cpp)
+ for (const auto& [id, decoder] : m_videoDecoders)
+ {
+     if (id.value == videoDecoderId.value)
+     {
+         return decoder;
+     }
+ }
+ return nullptr;
```

## Follow-up checklist

To fully align the header with the implementation (and make the change compile), the following concrete alignment is implied by the `.cpp`:

1. Declare the `m_videoDecoders` member in the header with the container type that supports `emplace_back(id, decoder)` and `for (const auto& [id, decoder] : ...)`.
2. Remove or repurpose `m_decoders` if it is no longer used.
3. Ensure `m_supportedModes` is either populated from `ensureInitializedLocked()` or returned directly by `getSupportedOperationalModes()` consistently (currently `getSupportedOperationalModes()` builds a local vector and does not use `m_supportedModes`).

This note does not apply code changes; it only records the exact observed code behavior and differences.
