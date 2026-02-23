<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/Other/avbuffer-hfp-loading-and-usage.md
- Operation: write
- Timestamp: 2026-02-19T10:55:49.824578+00:00
- Restored At: 2026-02-23T05:04:11.254255+00:00
- Task ID: cm219d4578
-->

# AVBuffer HFP Loading and Usage (YAML via ut_kvp)

## Purpose and analogy to the HDMI CEC UT controller pattern

The HDMI CEC controller example shows a common pattern used in this workspace:

1. Initialize a UT subsystem (in that case, the UT control plane).
2. Receive YAML payloads as `ut_kvp_instance_t*` in callbacks.
3. Convert the parsed payload back to text using `ut_kvp_getData()` and queue/consume it.

AVBuffer HFP (HAL Feature Profile) usage is analogous, but instead of receiving YAML over the control plane, the AVBuffer service loads a YAML profile from disk (or another source), parses it with `ut_kvp`, and then consumes specific fields to configure behavior. The intent is the same: treat YAML as the authoritative configuration input, parse it once, validate it, and then use it consistently in the component’s implementation.

In the TEVDevice AVBuffer implementation, this is done during `AvBufferManager` construction (service startup), using `ut_kvp_open()` to parse a YAML file into a `ut_kvp_instance_t`.

## Where AVBuffer reads HFP YAML from

AVBuffer needs a deterministic way to locate its HFP YAML. In the current implementation, AVBuffer resolves the HFP YAML path in this precedence order:

1. The environment variable `AVBUFFER_HFP_PATH`.
2. The static member `com::rdk::hal::avbuffer::AvBufferManager::_avbufferHFPPath` (intended to be set by the service entrypoint before publishing the Binder service).
3. A hard-coded default relative path: `vcomponent_configurations/hfp-avbuffer.yaml`.

This logic lives in:

- `10002556%2FTEVDevice/src/aidl/vcomponent_AvBufferManager.cpp` in `AvBufferManager::loadHfpConfigOrThrow()`.

The default HFP YAML file currently shipped with this repo is:

- `10002556%2FTEVDevice/vcomponent_configurations/hfp-avbuffer.yaml`

## YAML structure and keys (AVBuffer-specific)

The AVBuffer HFP YAML used by this implementation is structured like:

- Top-level node `avbuffer`
- Scalar fields under that node:
  - `nonSecureHeapBytes`
  - `secureHeapBytes`
  - `DecoderID`

Example:

```yaml
avbuffer:
  interfaceVersion: current
  nonSecureHeapBytes: 28311552
  secureHeapBytes: 16000000
  DecoderID: 2
```

The `DecoderID` field is used by this implementation as a platform policy parameter that affects both decoder-id validation and pool sizing.

## How to parse YAML using ut_kvp (file input)

### Step-by-step parsing flow

The canonical pattern for file-based parsing is:

1. Create a KVP instance:
   - `ut_kvp_instance_t* instance = ut_kvp_createInstance();`

2. Load and parse the YAML file:
   - `ut_kvp_status_t st = ut_kvp_open(instance, (char*)path);`

3. Extract fields using the KVP getters:
   - For string fields: `ut_kvp_getStringField(instance, "key/path", buf, bufSize)`
   - For numeric fields: `ut_kvp_getUInt64Field(instance, "key/path")` etc., or read as string and do strict parsing (recommended when you need explicit validation rules and better error reporting).

4. Destroy the instance:
   - `ut_kvp_destroyInstance(instance);`

This pattern is implemented in AVBuffer in:

- `AvBufferManager::loadHfpConfigOrThrow()` in `vcomponent_AvBufferManager.cpp`.

### Important ut_kvp path semantics (slash paths, dot conversion, and sequences)

`ut_kvp` supports hierarchical access using slash-separated paths like `avbuffer/nonSecureHeapBytes`.

It also supports dot-separated keys by converting dots (`.`) to slashes (`/`) internally. This is explicitly tested in `ut-control-17/tests/src/ut_test_kvp.c`, for example:

- `decodeTest.checkUint16IsDeadHex` works like `decodeTest/checkUint16IsDeadHex`.

For sequences, `ut_kvp_getListCount(instance, "path/to/list")` gives the number of elements, and you can index with `.../0`, `.../1`, etc. This is also tested in `ut_test_kvp.c`:

- `ut_kvp_getListCount(..., "decodeTest/checkStringList")`
- `ut_kvp_getStringField(..., "decodeTest/checkStringList/0", ...)`

These semantics matter if AVBuffer needs to consume decoder IDs from VTS-style HFP YAMLs (which typically represent resources as lists).

## Validating and consuming sizing parameters (heap sizes)

### What AVBuffer consumes today

At startup, `AvBufferManager::loadHfpConfigOrThrow()` reads:

- `avbuffer/nonSecureHeapBytes` (required)
- `avbuffer/secureHeapBytes` (required)
- `avbuffer/DecoderID` (required in this implementation)

It retrieves these fields as strings using `ut_kvp_getStringField()` and then performs strict integer parsing using local helper functions:

- `parseUint64Strict()` for heap byte sizes
- `parseIntStrict()` for `DecoderID`

This strict parsing rejects:
- Missing fields (key not found)
- Empty values
- Non-decimal values or trailing characters (the strict helpers accept only full-string decimal digits)
- Zero or negative values (heap sizes must be `> 0`; `DecoderID` must be `> 0`)

If the YAML is missing required keys or values are invalid, the function throws `std::runtime_error`, causing service startup to fail fast.

### How sizing parameters are used

After successful parsing, AVBuffer constructs its heaps using the parsed sizes:

- Non-secure heap: `new HeapImpl(Heap::HEAP_NON_SECURE, _nonSecureHeapBytes)`
- Secure heap: `new HeapImpl(Heap::HEAP_SECURE, _secureHeapBytes)` (only if secure support is compiled in; the current service rejects secure heap pool creation at runtime)

This happens in the `AvBufferManager` constructor after `loadHfpConfigOrThrow()` succeeds.

## Validating and consuming decoder IDs (AVBuffer-specific guidance)

### What “decoder ID” means in AVBuffer

In the current TEVDevice AVBuffer implementation, `_decoderID` is loaded from HFP `avbuffer/DecoderID` and treated as a decoder-count / upper bound parameter. It is used in two places:

1. Decoder ID validation: `createVideoPool()` and `createAudioPool()` enforce a range check:
   - Video: `0 <= videoDecoderId.value < _decoderID`
   - Audio: `audioDecoderId.value` is validated the same way unless it equals `IAudioDecoder::Id::UNDEFINED` (which is explicitly allowed by the AIDL contract and therefore treated as valid).

2. Pool sizing policy: requested pool size is computed as:
   - `requestedPoolSize = heap->GetSize() / _decoderID` (falling back to full heap size if the division results in zero).

This makes `_decoderID` a critical HFP-derived parameter because it influences how heap capacity is partitioned across decoder resources and which decoder IDs are accepted.

### AVBuffer-specific recommendations for decoder ID validation

If the goal is to align AVBuffer with the VTS-style “decoder IDs come from decoder HFP YAML” approach, the strongest validation strategy is:

1. Read the real list of decoder IDs from the AudioDecoder and VideoDecoder HFP YAMLs (VTS-style YAMLs).
2. Build allow-lists (or min/max bounds) for acceptable IDs.
3. In `createVideoPool()` and `createAudioPool()` validate against those allow-lists rather than against a generic count.

This is more accurate than using a simple `0..N-1` range check, especially if IDs are sparse or not contiguous.

### How to consume VTS-style decoder HFP YAML with ut_kvp

The repo already contains VTS-style YAML examples:

- `10002556%2FTEVDevice/vcomponent_configurations/hfp-videodecoder.yaml`
- `10002556%2FTEVDevice/vcomponent_configurations/hfp-audiodecoder.yaml`

These are structured as resource lists under manager nodes:

```yaml
IVideoDecoderManager:
  IVideoDecoder:
    - id: 0
    - id: 1
    - id: 2
```

```yaml
IAudioDecoderManager:
  IAudioDecoder:
    - id: 0
    - id: 1
    - id: 2
  availableDecoders:
    - 0
    - 1
    - 2
```

Using `ut_kvp`, the implementation pattern to extract IDs is:

1. Parse the decoder HFP file into a KVP instance:
   - `ut_kvp_open(instance, "vcomponent_configurations/hfp-videodecoder.yaml")`

2. Count items in the decoder list:
   - `count = ut_kvp_getListCount(instance, "IVideoDecoderManager/IVideoDecoder")`
   - `count = ut_kvp_getListCount(instance, "IAudioDecoderManager/IAudioDecoder")`

3. For each element, fetch `id`:
   - key format: `"IVideoDecoderManager/IVideoDecoder/<i>/id"`
   - key format: `"IAudioDecoderManager/IAudioDecoder/<i>/id"`

Because `ut_kvp_getStringField()` expects scalars, `id` must be a scalar in YAML (as shown).

4. Parse and validate:
   - Use strict decimal parsing (as AVBuffer currently does for its own HFP) to ensure IDs are integers.
   - Validate uniqueness (no duplicates).
   - Optionally validate that `availableDecoders` (if present) matches the `IAudioDecoder` entries.

5. Use the resulting set/vector to validate `audioDecoderId.value` and `videoDecoderId.value` in `createAudioPool()` / `createVideoPool()`.

This approach directly leverages the list and indexing semantics demonstrated in `ut-control-17/tests/src/ut_test_kvp.c`.

## How to structure AVBuffer’s HFP handling in code (recommended pattern)

Even though AVBuffer currently loads only `hfp-avbuffer.yaml`, an AVBuffer HFP implementation that is explicitly “analogous” to the HDMI CEC controller pattern (parse YAML into KVP, extract, validate, then consume) should be organized into these conceptual steps:

1. Resolve configuration inputs (paths):
   - Determine `AVBUFFER_HFP_PATH` for AVBuffer sizing policy.
   - Determine AudioDecoder and VideoDecoder HFP paths if decoder IDs are to be derived from decoder HFPs.

2. Parse YAML into `ut_kvp_instance_t`:
   - File-based: `ut_kvp_open()`
   - In-memory (optional): `ut_kvp_openMemory()` if the YAML arrives via some control-plane message or IPC (this is directly analogous to how HDMI CEC receives messages and uses `ut_kvp_getData()`; you can store YAML text, then later parse it into KVP when needed).

3. Extract fields using `ut_kvp_getStringField()` or numeric getters:
   - Prefer `ut_kvp_getStringField()` + strict parsing when fields are required and you want consistent error handling and explicit bounds checking.

4. Validate:
   - Heap sizes must be positive and within implementation limits.
   - Decoder IDs must be consistent with decoder HFPs (if used) or with a platform-defined policy.
   - Any “count” derived from IDs should be checked for non-zero to avoid divide-by-zero in pool sizing.

5. Consume:
   - Store validated values in `AvBufferManager` members (e.g., `_nonSecureHeapBytes`, `_secureHeapBytes`, decoder allow-lists).
   - Use those members in API entry points (`createVideoPool`, `createAudioPool`, etc.) for consistent behavior.

## Notes on error-handling policy

The HDMI CEC controller code returns `false` on init failure; it is a controller, not a HAL service constructor.

AVBuffer is a Binder service. In the current code, HFP parsing is treated as a startup-critical requirement: `loadHfpConfigOrThrow()` throws on missing/invalid fields, and the constructor does not catch the exception, causing service startup to fail fast. This is a deliberate policy choice.

If AVBuffer needs a “soft-fail and continue” policy (like the controller pattern), the alternative is:
- Load HFP in a `NoThrow` function,
- Log and keep defaults when keys are missing,
- Proceed with conservative defaults.

However, this repo’s current AVBuffer implementation is explicitly “strict” for required keys.

## Minimal checklist for implementing HFP in AVBuffer

AVBuffer HFP loading/usage is correct when:

1. The YAML path resolution is deterministic and documented (env override, static path, default).
2. YAML is parsed through `ut_kvp_createInstance()` + `ut_kvp_open()`.
3. Values are extracted from keys under `avbuffer/...` and validated strictly.
4. Heap sizes are applied to heap creation.
5. Decoder ID policy is applied consistently:
   - Validation of incoming decoder IDs uses either a strict allow-list from decoder HFP YAMLs or a clearly defined range policy.
   - Pool sizing uses validated decoder count / ID policy without division-by-zero or overflow risks.
6. The `ut_kvp_instance_t` is always destroyed after parsing to avoid leaks.
