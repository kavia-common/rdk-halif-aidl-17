<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/Other/tunneling-non-tunneling-sequential-concurrent.md
- Operation: write
- Timestamp: 2026-02-18T04:47:33.579180+00:00
- Restored At: 2026-02-23T05:04:11.252520+00:00
- Task ID: cm219d4578
-->

# Tunneling vs Non-tunneling, Sequential vs Concurrent (Project Context)

## Purpose

This document explains the terms “tunneling”, “non-tunneling”, “sequential”, and “concurrent” as they are used in this workspace’s decoder pipeline documentation under `kavia-docs/`. The intent is not to define generic multimedia terminology, but to ground the definitions in the specific HALIF AIDL pipelines described here (Video Decoder, Audio Decoder, AVBuffer, and Audio Sink).

## Tunneling vs non-tunneling (as used in this repository)

In this repository, tunneling vs non-tunneling is primarily about whether decoded media buffers are returned to the client process as `IAVBuffer` handles, or whether decoded media is consumed inside the vendor layer without returning decoded buffers across the AIDL boundary.

### Non-tunneling (non-tunnelled) mode

In non-tunneling mode, the decoder returns decoded output back to the client using the controller listener callback, and that decoded output is represented by an AVBuffer handle.

For Audio Decoder, the non-tunnelled flow is explicitly described as decoded PCM being returned to the client and then queued to Audio Sink:

The typical sequence is:
1. The client submits an encoded input handle via `IAudioDecoderController.decodeBuffer(...)`.
2. The decoder produces decoded PCM and calls `IAudioDecoderControllerListener.onFrameOutput(...)` with a valid PCM `frameBufferHandle`.
3. The client queues that PCM handle into Audio Sink with `IAudioSinkController.queueAudioFrame(...)`.
4. Audio Sink eventually frees the PCM handle via `IAVBuffer.free(...)`.

This is reflected in the Audio Decoder pipeline documents, which describe “PCM handoff to Audio Sink (non-tunnelled mode)” and show the explicit `queueAudioFrame(...)` step after `onFrameOutput(...)`.

For Video Decoder, non-tunnelled mode similarly means:
1. The client calls `IVideoDecoderController.decodeBuffer(ptsNs, bufferHandle)`.
2. The decoder calls `IVideoDecoderControllerListener.onFrameOutput(ptsNs, frameBufferHandle, metadata)` where `frameBufferHandle` is a valid decoded frame handle.
3. The decoded frame handle is passed downstream for presentation and later freed.

This repository documentation also links non-tunnelling behavior to backpressure: if the vendor-managed output frame pool is exhausted, `decodeBuffer(...)` may return `false` and output may be blocked until downstream frees output handles.

### Tunneling (tunnelled) mode

In tunneling mode, the decoder does not return decoded frame/PCM buffers to the client. Decoded media is “tunnelled” through the vendor layer (that is, handled internally rather than being exported as decoded buffer handles to middleware).

In the documentation here, tunnelling is expressed concretely through callback behavior:

For Video Decoder:
- In `TUNNELLED` mode, `onFrameOutput(...)` does not return a decoded frame handle. When metadata must still be delivered, `onFrameOutput(...)` is invoked with `frameBufferHandle = -1`.
- If operating exclusively in tunnelled mode and there is no metadata to pass, no call to `onFrameOutput()` should be made.

For Audio Decoder, the pipeline documents describe the same pattern:
- In tunnelled mode, the client does not receive a PCM handle; instead `frameBufferHandle = -1` is used when metadata (or EOS) must be sent.
- Because no PCM handle is returned to the client, the client does not call `queueAudioFrame(...)` into Audio Sink as part of the core data path.

### What does not change between tunneling and non-tunneling

Across both modes in this repository’s documentation:
- The client still submits encoded/compressed input via `decodeBuffer(...)` using AVBuffer handles allocated from AVBuffer pools.
- Ownership rules for input buffers still apply: once `decodeBuffer(...)` accepts the input (returns `true`), the decoder owns the input handle and must free it via `IAVBuffer.free(...)` after processing, including on `FLUSHING`/`STOPPING` paths.

## Sequential vs concurrent (as used in this repository)

In this workspace, “sequential” and “concurrent” are best understood as properties of how the pipeline behaves over time and across threads/components, rather than as a single configuration flag.

### Sequential (ordering constraints and required call sequencing)

“Sequential” in this documentation appears in two closely related ways:

First, there is required sequencing of control-plane operations due to the HAL session state machine. For example, the Audio Decoder control flow documentation describes a strict lifecycle such as:
- `open(...)` to move from `CLOSED` to `READY`,
- `start()` to move into `STARTED`,
- data-plane calls like `decodeBuffer(...)` while started,
- then `stop()` back to `READY`,
- then `close(...)` to `CLOSED`.

These are sequential constraints: certain calls are only valid after earlier calls have been made and the session reaches the appropriate state.

Second, there are sequential ordering rules for media output. For Video Decoder, the repository documentation states that frames are output in presentation order regardless of input order, and that output callbacks (`onFrameOutput(...)`) carry the same `nsPresentationTime` time base used at input. That is a sequential behavior: outputs must respect a defined ordering (presentation order) even if the implementation is internally parallel.

### Concurrent (overlap of work and/or simultaneous modes)

“Concurrent” in this repository context is about overlap and parallelism in the pipeline:

1. The decoder pipeline is described as asynchronous: `decodeBuffer(...)` is a submission point that can enqueue work, while decode and output occur later via callbacks (`onFrameOutput(...)`). This implies the client’s submission and the decoder’s processing can be concurrent in time.

2. The Video Decoder documentation in `kavia-docs/video-decoder-control-flow.md` explicitly describes a case of concurrency between operational modes:
- `GRAPHICS_TEXTURE` mode “can run concurrently with tunnelled or non-tunnelled mode.”

This is a concrete repository-defined use of the term concurrent: enabling an additional output/processing mode at the same time as one of the primary operational modes.

3. Backpressure behaviors also imply concurrency: the decoder can continue holding queued input buffers while the downstream pipeline consumes output buffers, and `decodeBuffer(...)` may return `false` when internal resources are full. This reflects concurrent producers/consumers across the pipeline components (client producing input, decoder producing output, sink consuming output).

## Practical summary mapping (how to recognize each mode in the APIs)

### Tunneling vs non-tunneling: observable behavior at `onFrameOutput(...)`

You can infer the mode by what you receive in the controller listener callback:

- Non-tunneling: `onFrameOutput(..., frameBufferHandle=<valid handle>, ...)`
- Tunneling: `onFrameOutput(..., frameBufferHandle=-1, ...)` (metadata-only), and no decoded handles are returned.

### Sequential vs concurrent: observable behavior across control vs data plane

- Sequential: control-plane call order is constrained by state (open → start → decodeBuffer → stop → close), and output is ordered by presentation time.
- Concurrent: decode work and callbacks can overlap with client submission; and in Video Decoder, `GRAPHICS_TEXTURE` is documented as able to run concurrently with tunnelled/non-tunnelled.

## References (documents in this repository)

This explanation is derived from the following `kavia-docs/` documents:

- `kavia-docs/decoder-pipeline-sequence.md`
- `kavia-docs/decoder-pipeline-components.md`
- `kavia-docs/decoder-pipeline-control-flow.md`
- `kavia-docs/decoder-pipeline-data-flow.md`
- `kavia-docs/video-decoder-control-flow.md`
- `kavia-docs/video-decoder-data-flow.md`
- `kavia-docs/video-decoder-end-to-end-code-flow-client-server-with-and-without-ffmpeg.md`
