<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/Other/tevdevice-avbuffer-triggering-policy-ppt-outline.md
- Operation: write
- Timestamp: 2026-02-02T09:38:51.558284+00:00
- Restored At: 2026-02-23T05:04:11.250063+00:00
- Task ID: cm219d4578
-->

# TEVDevice AVBuffer Triggering Policy (3-Slide PPT Outline)

## Purpose

This document provides a concise, customer-facing 3-slide outline for explaining the TEVDevice vDevice AVBuffer triggering policy work. It focuses on overall status, default heap sizing, pool count/capacity limits, and which existing diagrams can be reused as slide visuals.

This outline is anchored primarily on the vDevice AVBuffer design document, and it supplements numerical “defaults” from the existing workspace AVBuffer reference-design documents where the vDevice design document is intentionally implementation-agnostic.

## Source notes (what is and is not in vDevice_AVBuffer.md)

The file `kavia-docs/vDevice_AVBuffer.md` defines the AVBuffer concepts, roles, and the intended allocation + notify callback flow, and it contains reusable Mermaid diagrams. It does not, by itself, specify concrete numeric defaults (heap size, pool sizes, exact max pools). Concrete numeric defaults are pulled from other existing workspace documents that describe the current TEVDevice/reference configuration and behaviors.

## Slide 1: What AVBuffer is, and current status of “triggering policy”

### Slide title

AVBuffer: Memory pools + handles, and “space available” notifications

### Customer-facing narrative

AVBuffer is a vendor-layer service that creates pools inside a heap and returns opaque allocation handles for clients to allocate and free buffer memory. When an allocation fails with out-of-memory, clients can register interest in being notified later, via `notifyWhenSpaceAvailable(pool, size)` and an `IAVBufferSpaceListener.onSpaceAvailable()` callback. The design intent is to let clients avoid polling and to safely “retry on signal.”

In the current workspace state, the design is documented and the notification intent is recorded, but the end-to-end triggering policy (the part that decides when to fire `onSpaceAvailable()`) is not fully wired/implemented yet.

### Status callouts (slide text)

The following statements are appropriate as a simple status block on the slide:

- The `notifyWhenSpaceAvailable()` API and listener concept are part of the design and interface flow.
- The service should evaluate “space has become available” after frees and notify the client listener without risking deadlocks.
- In the current TEVDevice/reference state, notification intent is recorded but callback triggering is not fully implemented end-to-end.

### Visual suggestion (diagram)

Reuse the “create pools, alloc, free” sequence diagram from the vDevice design doc and highlight the out-of-memory and callback parts:

- Source: `kavia-docs/vDevice_AVBuffer.md` → section “Sequence Diagram (create pools, alloc, free)”.

If you need a smaller visual for Slide 1, crop to just the alloc → OOM → notifyWhenSpaceAvailable → free → onSpaceAvailable path.

## Slide 2: Defaults and capacity model (heap size, pool counts, max pools)

### Slide title

Default heap and pool capacity (what fits, and why)

### Customer-facing narrative

The AVBuffer service reserves memory at the heap level, then carves that heap into pools. In the TEVDevice/reference configuration documented in this workspace, the non-secure heap is backed by shared memory and is sized at 27 MiB. Pools are then created with a policy that is tied to a “max allowed decoder id” constant, and pool handles are selected from a bounded ID range. Taken together, these factors determine the practical maximum number of pools that can exist concurrently and the maximum per-pool size.

### Default numeric data points (for a simple table)

These values are explicitly described in the existing workspace documents:

- Default non-secure heap size: 27 MiB (27 × 1024 × 1024 bytes).
- Pool sizing policy (TEVDevice/reference): poolSize = heapSize / MAX_ALLOWED_DECODER_ID.
- MAX_ALLOWED_DECODER_ID: 2.
- Implied pool size under this policy: 27 MiB / 2 = 13.5 MiB per pool.
- Pool handle selection range: 0..127 (with -1/0xFF reserved as invalid).
- Practical maximum number of pools concurrently in the default non-secure heap, given the pool sizing policy: 2 pools (because each pool reserves half the heap).

If you must report “video pool count” as a simple number, the most defensible customer-facing phrasing is: “The default policy is sized around up to 2 decoder IDs (MAX_ALLOWED_DECODER_ID = 2).”

### Visual suggestion A (stacked capacity bar)

A single stacked bar representing the default 27 MiB heap:

- Total: 27 MiB
- Segment 1: 13.5 MiB pool
- Segment 2: 13.5 MiB pool

Label the visual as “Default non-secure heap capacity partitioned into two pools by policy (heap/2).”

### Visual suggestion B (small “limits” table)

A small 2-column table on the slide:

- Heap: 27 MiB (default)
- MAX_ALLOWED_DECODER_ID: 2
- Pool size per pool: 13.5 MiB (default policy)
- Max pool handles: 128 possible IDs (0..127), but practical max pools is constrained by heap capacity

### Visual suggestion C (optional: alternate pool-size defaults)

If your audience is already familiar with the “27 MiB heap split into 25 MiB video + 2 MiB audio” configuration, you can include it as an optional “reference configuration example.” However, ensure the slide clarifies that this is an example from a different reference description and not the implementation-agnostic vDevice design document.

## Slide 3: Triggering policy flow (OOM → notify → free → callback) and what “done” looks like

### Slide title

How triggering works: from out-of-memory to onSpaceAvailable

### Customer-facing narrative

The intended recovery flow is simple: when allocation fails due to insufficient space, the client registers a notification threshold (“tell me when at least N bytes are free”). When a buffer is freed, the service reevaluates whether any pending thresholds are now satisfiable and, if so, signals the client via `onSpaceAvailable()`. The callback is intentionally lightweight and does not carry a pool ID; clients treat it as a “wake up and retry allocation” signal.

A key implementation requirement is to avoid deadlocks and lock re-entrancy by not invoking callbacks while holding global allocator locks.

### Triggering policy statements (slide text)

- Trigger condition: after `free(handle)`, recompute pool free space and compare against any pending notify thresholds for that pool.
- Action: call `IAVBufferSpaceListener.onSpaceAvailable()` asynchronously once the condition is met.
- Client behavior: retry `alloc(pool, size)` after receiving the callback.
- Concurrency constraint: capture listener references under lock, but invoke callbacks after releasing allocator locks.

### Visual suggestion (simplified sequence)

Use a simplified 5-step flow diagram (suitable for a slide) derived from the vDevice sequence diagram:

1. Client: `alloc(pool, size)`
2. Service: returns/throws OUT_OF_MEMORY
3. Client: `notifyWhenSpaceAvailable(pool, size)`
4. Another client (or same client later): `free(handle)`
5. Service → Listener: `onSpaceAvailable()` (client retries alloc)

### Relevant diagrams to reuse

- Sequence diagram (recommended for Slide 3): `kavia-docs/vDevice_AVBuffer.md` → “Sequence Diagram (create pools, alloc, free)”.
- Architecture/class relationships diagram (optional, for appendix or speaker notes): `kavia-docs/vDevice_AVBuffer.md` → “Relationships diagram (role-level)”.

## References (files in this workspace)

- Primary design document with diagrams and API flows: `kavia-docs/vDevice_AVBuffer.md`
- Current workspace AVBuffer summary including notification status notes: `kavia-docs/avbuffer.md`
- TEVDevice/reference AVBuffer design notes including known gaps: `kavia-docs/AVBuffer.md`
- TEVDevice manager design notes including heap size, pool handle range, and pool sizing policy: `kavia-docs/avbuffer-manager-design.md`
- Additional reference discussion of pool sizes/heap sizing in a reference implementation (use only if needed): `kavia-docs/avbuffer-reference-code-flow-client-perspective.md`
