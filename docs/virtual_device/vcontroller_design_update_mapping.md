# VController (vDevice Controller) – HLD-to-Design Mapping Notes (V0.0.3)

## Purpose
This document captures **extracted VController requirements** from the provided “Virtual Device Development – Requirement & Technical Approach” HLD (Ver 0.0.3, 25-Nov-2025) and maps them into **concrete update points** for the VController/vDevice Controller design markdown used in the documentation site.

Source input: `attachments/...Requirement_HighLevelDesign for Virtual_Design V0.0.3...txt`

Related existing docs (referenced by MkDocs nav, typically sourced from ut-core wiki):
- `external_content/ut-core-wiki/5.1.0:-Standards:-vDevice-Controller.md`
- `external_content/ut-core-wiki/5.1.1:-Standards:-vDevice-Control-Plane.md`

---

## 1. Extracted VController functionality from the HLD (V0.0.3)

### 1.1 Explicit responsibilities (Framework Components → vController)
- **Coordinator/orchestrator**
  - “Configures each vComponent” and “acts as main coordinator.”
- **Control plane gateway**
  - “Handles the control plane socket” and routes control messages to appropriate vComponent instances.
- **WebSocket client to vComponents**
  - “Connects to vComponent over Websocket as client.”
- **Connection registry**
  - “Maintains the list of the ports and websocket address for VComponent communication.”

### 1.2 Inputs and boot/initialization context
- Bootloader passes **Hardware Profile (HFP/HPF) YAML** to kernel (local path or cloud; cloud is out-of-scope for MVP).
- Service Manager comes up in boot sequence.
- vComponents register with Service Manager for Binder IPC.
- vController configures vComponents using the HFP/HPF.

### 1.3 Control messages and routing semantics
- Control Plane sends messages in **YAML format**.
- vComponents:
  - receive messages asynchronously from vController,
  - open a port for configuration messaging at service initialization,
  - expect message formats to be pre-defined,
  - receive messages based on **KVP (Key Value Pair)**, **not** by port number registered with control plane.

### 1.4 MVP acceptance constraints
- VController must configure vDevice/vComponents for **1 STB and 1 Panel**.
- “All control plane messages should be routed through the vController.”
- Control plane ↔ vController communication must be supported.

---

## 2. Mapping notes: what to update in the VController design MD

### 2.1 Terminology alignment: HFP/HPF vs platformProfile.yaml
The existing “Controller” documentation (RDK Hardware Porting Kit) uses **platformProfile.yaml** terminology.
The HLD uses **HFP/HPF YAML**.

**Update required in design MD**
- Add a short clarification section:
  - Whether HFP/HPF is the same artifact as platformProfile.yaml in practice.
  - How the VController locates/loads it (kernel-provided path vs other mechanism).
  - MVP scope: “cloud profile retrieval” is out-of-scope; local profile is in-scope.

### 2.2 WebSocket topology and port registry (must be explicit)
HLD requires VController to:
- handle control plane socket (ingress),
- connect to each vComponent as a WebSocket client (egress),
- maintain list of ports/addresses per vComponent.

**Update required in design MD**
- Add “Connectivity model” section with:
  - Control Plane → VController (socket/server/listener).
  - VController → vComponents (WS client connections).
  - vComponent endpoints: per-component WS address/port.
  - Registry responsibilities and lifecycle (discovery vs static config).

### 2.3 Routing rules: KVP-based dispatch (not port-based)
HLD states routing is KVP-based and not based on port number.

**Update required in design MD**
- Add “Message routing” section:
  - YAML message envelope assumptions.
  - Required KVP keys used to determine vComponent destination(s).
  - Broadcast/multicast vs unicast handling.
  - Unknown key / unsupported message handling behavior (log/reject/etc.).

### 2.4 Boot sequence (align with HLD)
**Update required in design MD**
- Update/insert “System workflow (boot sequence)” to reflect:
  1. Bootloader passes HFP/HPF YAML to kernel.
  2. Service Manager starts.
  3. vComponents register (Binder IPC).
  4. VController loads HFP/HPF and configures vComponents; builds/uses WS endpoint registry.
  5. Control Plane traffic routed via VController.
  6. VTS validates components.

### 2.5 MVP constraints & acceptance criteria should be stated
**Update required in design MD**
- Add an “MVP scope” subsection:
  - Support configuration for 1 STB and 1 Panel.
  - All control plane messages route through VController.
  - Host-independence and x86/QEMU context (HLD scope statement).
  - Logging alignment callout (RDKE logging infrastructure requirement).

---

## 3. Suggested section outline for the updated VController design MD
1. Overview / Purpose
2. Inputs
   - HFP/HPF (platform profile) format and acquisition path
3. Key Components and Roles
   - VController, vComponent, Control Plane, Service Manager
4. Connectivity Model
   - Control Plane socket (ingress)
   - VController as WebSocket client to vComponents (egress)
   - Endpoint registry (ports/addresses)
5. Message Format and Routing
   - YAML message structure
   - KVP-based routing
6. Boot / Initialization Sequence
7. Error Handling and Observability
8. MVP Scope / Constraints
9. Open Points (if needed)
   - Clarify HFP vs platformProfile naming
   - Confirm exact KVP keys used by VTS for routing

---

## 4. Planned update steps (implementation plan)
1. Pull current “vDevice Controller” markdown content (from the ut-core wiki source referenced by MkDocs).
2. Diff current content vs these mapping notes.
3. Apply edits focusing on:
   - HFP/HPF terminology and input path,
   - WS client-to-vComponent + endpoint registry,
   - KVP-based routing,
   - boot sequence and MVP constraints.
4. Ensure links to Control Plane and HAL Feature Profiles are correct.
5. Build/serve MkDocs to validate rendering and navigation.
6. Final consistency pass (terminology: “vController” vs “vDevice Controller”).

---
End of mapping notes.
