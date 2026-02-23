<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/videodecoder_documents/video-decoder-sequence-diagram.md
- Operation: write
- Timestamp: 2026-02-12T04:51:54.964105+00:00
- Restored At: 2026-02-23T05:04:11.251982+00:00
- Task ID: cm219d4578
-->

# Video Decoder Sequence Diagram (HALIF AIDL)

This document is derived only from the following sources:

- Video Decoder states: https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/video_decoder/current/video_decoder/#video-decoder-states
- AV Buffer: https://rdkcentral.github.io/rdk-halif-aidl/0.13.0/halif/av_buffer/current/av_buffer/
- Video decoder AIDL definitions: https://github.com/rdkcentral/rdk-halif-aidl/tree/main/videodecoder/current/com/rdk/hal/videodecoder

```mermaid
sequenceDiagram
    box rgb(30,136,229) "RDK Video Decoder"
        participant Client as "RDK Client"
        participant EvL as "IVideoDecoderEventListener"
        participant CtrlL as "IVideoDecoderControllerListener"
    end
    box rgb(249,168,37) "Video Decoder Server"
        participant VD as "IVideoDecoder"
        participant Ctrl as "IVideoDecoderController"
    end
    box rgb(67,160,71) "Video AV Buffer"
        participant AVB as "IAVBuffer"
    end

    Client->>VD: "registerEventListener(IVideoDecoderEventListener)"

    Note over VD: "open() transitions from CLOSED -> OPENING -> READY"

    Client->>VD: "open(Codec, secure, IVideoDecoderControllerListener)"
    VD-->>EvL: "onStateChanged(CLOSED -> OPENING)"
    VD->>Ctrl: "new"
    VD-->>EvL: "onStateChanged(OPENING -> READY)"
    VD-->>Client: "IVideoDecoderController"

    Note over VD: "start() transitions from READY -> STARTING -> STARTED"

    Client->>Ctrl: "start()"
    VD-->>EvL: "onStateChanged(READY -> STARTING)"
    VD-->>EvL: "onStateChanged(STARTING -> STARTED)"

    Note over Client: "Client can now send AV buffers"

    Client->>Ctrl: "decodeBuffer(pts, bufferHandle=1)"
    Client->>Ctrl: "decodeBuffer(pts, bufferHandle=2)"
    Ctrl-->>CtrlL: "onFrameOutput(pts, frameBufferHandle=1000, metadata)"
    Ctrl->>AVB: "free(bufferHandle=1)"

    Note over VD: "flush() transitions from STARTED -> FLUSHING -> STARTED"

    Client->>Ctrl: "flush(reset)"
    VD-->>EvL: "onStateChanged(STARTED -> FLUSHING)"
    Ctrl->>AVB: "free(bufferHandle=2)"
    VD-->>EvL: "onStateChanged(FLUSHING -> STARTED)"

    Client->>Ctrl: "decodeBuffer(pts, bufferHandle=3)"

    Note over VD: "stop() transitions from STARTED -> STOPPING -> READY"

    Client->>Ctrl: "stop()"
    VD-->>EvL: "onStateChanged(STARTED -> STOPPING)"
    Ctrl->>AVB: "free(bufferHandle=3)"
    VD-->>EvL: "onStateChanged(STOPPING -> READY)"

    Note over VD: "close() transitions from READY -> CLOSING -> CLOSED"

    Client->>VD: "close(IVideoDecoderController)"
    VD-->>EvL: "onStateChanged(READY -> CLOSING)"
    VD->>Ctrl: "delete"
    VD-->>EvL: "onStateChanged(CLOSING -> CLOSED)"

    Client->>VD: "unregisterEventListener(IVideoDecoderEventListener)"
```
