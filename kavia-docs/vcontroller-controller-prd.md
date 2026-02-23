<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/FeatureSpecs/vcontroller-controller-prd.md
- Operation: write
- Timestamp: 2026-01-09T11:55:57.748753+00:00
- Restored At: 2026-02-23T05:04:11.242413+00:00
- Task ID: cm219d4578
-->

# PRD: VController / Controller (vDevice Controller) for RDK Virtual Device and HPK

## Purpose and background

This Product Requirements Document (PRD) defines the product goals, scope, and acceptance criteria for the VController/Controller project, also referred to as the vDevice Controller. The vDevice Controller is the central orchestrator for the RDK-E “vDevice” virtual target, coordinating vendor-layer modules (“vComponents”), exposing a unified control plane integration point, and enabling deterministic development and testing without physical devices.

This PRD is populated from the attached vDevice High-Level Design (HLD) document “Virtual Device Development – Requirement & Technical Approach” (Ver 0.0.3, 25-Nov-2025) and the existing HLD-to-design mapping notes at `rdk-halif-aidl-17/docs/virtual_device/vcontroller_design_update_mapping.md`. It also aligns the Control Plane terminology with the RDK Hardware Porting Kit (HPK) Control Plane overview (ingested PDF excerpt).

## Goals and non-goals

### Goals

The VController/Controller shall provide a standardized and reusable orchestration layer that enables the following outcomes.

First, it shall support development, integration, and testing of RDK-E software without requiring physical devices by providing a software-based virtual device environment, suitable for running as a VM image (for MVP: on Linux host using QEMU). Second, it shall act as the main coordinator for the vendor-layer vComponents, managing their lifecycle and configuration. Third, it shall provide a unified control-plane integration in which control messages (YAML) can be used to control and configure the virtual device runtime. Fourth, it shall enable VTS testing of vendor-layer HALs in a profile-driven manner, where test runs can be parameterized by a “device profile” artifact.

### Non-goals

The VController/Controller is not intended to provide low-level implementation details for the internal HAL logic of each vComponent. It is also not intended to deliver commercial licensing policy or contract terms. For the MVP, it is not intended to deliver cloud retrieval of the device profile artifact, nor is it intended to solve hardware-grade security/tamper resistance or “secure video” support.

## Stakeholders and users

The intended stakeholders include RDK-E architects and platform owners who need a clear definition of what the vDevice Controller orchestrates and how it fits into the RDK software stack. HAL component owners and vendor-layer implementers need a standard mechanism for integrating their vComponents with lifecycle orchestration and control-plane-driven configuration. Test engineering and CI maintainers need deterministic configuration and a practical path to running VTS test suites in automation. Vendors integrating with HPK need clarity on what must be implemented, how to configure the system, and what conformance criteria apply for a vDevice environment.

## Product scope

### In-scope product capabilities

#### 1. Orchestration and lifecycle management

The vDevice Controller shall act as the main coordinator of the vendor-layer runtime and shall manage lifecycle operations for vComponent instances. This includes initializing the controller, loading configuration, instantiating and configuring vComponents per profile, and coordinating startup and shutdown.

The HLD describes that virtual components are registered with a Service Manager to enable Binder IPC communication. From a product perspective, the controller must integrate into this boot/runtime environment by using the profile to configure vComponents that are registered and available.

#### 2. Unified control plane integration and routing responsibility

The product shall support external control of the virtual device through control messages sent in YAML format. The HPK Control Plane documentation describes a WebSocket interface for vDevices where external users or automation tools send real-time control messages. In a vDevice setup, messages are routed via the vDevice Controller, which forwards the message to the appropriate vComponent.

For MVP alignment (from the mapping notes), all control plane messages intended for vDevice components shall be routed through the vController/vDevice Controller. This makes the controller the central routing responsibility even if vComponents expose configuration ports for receiving messages.

#### 3. Connectivity model: control plane socket ingress and WebSocket client egress

The HLD contains explicit connectivity requirements for the controller.

The controller shall handle the control plane socket and act as the routing gateway. Additionally, it shall connect to vComponents over WebSocket as a client. The controller must therefore maintain a registry of vComponent endpoints (ports and WebSocket addresses) used for outbound communication, with a deterministic lifecycle (loaded from profile and/or constructed during initialization).

#### 4. Message format and routing semantics (YAML and KVP-based routing)

The HLD states that control messages are sent in YAML format and that all vComponents receive messages based on KVP (Key Value Pair) semantics, not based on the port number by which they are registered with the control plane.

Product-wise, this means the messaging layer must support a pre-defined message format and a key-based routing contract. The controller must be able to interpret or classify incoming messages sufficiently to determine which vComponent(s) should receive the message, based on message keys rather than transport-level port routing.

#### 5. Profile-driven configuration (HPF/HFP and platformProfile terminology)

The HLD describes that U-Boot passes a Hardware Profile (HPF) YAML configuration file to the kernel, and the controller configures each vComponent using the HPF. The mapping notes highlight a terminology mismatch with older “Controller” documentation that uses `platformProfile.yaml`.

The product requirement is that the controller must support a well-defined profile input, and the documentation and interfaces must clarify how HPF/HFP relates to “platform profile” terminology. For MVP, profile retrieval from cloud is out-of-scope, and local profile selection and loading is in-scope.

#### 6. MVP device configurations and validation focus

The HLD acceptance criteria for the controller scope explicitly includes configuring the vDevice/vComponents for at least two platforms: one STB and one TV panel (or “Panel”). For this MVP, other platform profiles beyond the agreed ones are out-of-scope.

### Out of scope for MVP (explicit)

Cloud retrieval and validation of the HPF/HPF profile is out of scope for MVP. Secure AV pipeline support is out of scope, as are proprietary Dolby formats. Verification of VSI components such as Bluez, Wi-Fi, and OpenGL is out of scope. DMIPS performance monitoring tooling (called out as a requirement in the broader PRD context) is explicitly marked out of scope for Phase 1/MVP in the HLD.

## Constraints and assumptions

The product operates within constraints described by the HLD. The vDevice image is produced by a Yocto-based image assembler and booted in a VM. For MVP, virtualization is in a QEMU environment on a Linux host platform. The Apps and Middleware layers are intended to remain unchanged versus physical targets, and the vendor layer is swapped for a virtual implementation.

The boot flow includes U-Boot providing the device profile (HPF) to the kernel. The Service Manager is available as part of boot and is responsible for enabling Binder IPC communications by instantiating or hosting registered vComponents. The controller is expected to configure vComponents using the profile and provide routing of control plane messages.

The controller’s message handling assumes that control plane messages have a pre-defined schema and that routing can be expressed as key presence rules (KVP).

## Requirements

## Functional requirements

### FR-1: Controller shall orchestrate vComponent lifecycle

The controller shall manage vComponent initialization and configuration in a deterministic manner and coordinate startup and shutdown sequencing.

Acceptance criteria:
1. The controller starts reliably and can load its configuration/profile without crashing.
2. The controller can configure vComponents per the selected profile and trigger their initialization in a repeatable order.
3. The controller can shut down cleanly without leaving vComponents in an undefined lifecycle state.

### FR-2: Controller shall handle control plane socket ingress

The controller shall handle the control plane socket and accept control messages originating from external stimulus sources (test users, automation tools, CI).

Acceptance criteria:
1. The controller provides a single, well-defined ingress path for control messages for the vDevice setup.
2. Control messages can be delivered as YAML payloads and are accepted and processed without crashing.

### FR-3: Controller shall route all vDevice control messages through itself (MVP)

For a vDevice setup, the system shall route control messages through the vDevice Controller, which forwards messages to the appropriate vComponent(s).

Acceptance criteria:
1. For the vDevice mode, external control-plane stimulus targets the controller first, not individual vComponents directly.
2. The controller can forward messages to the appropriate vComponent(s) based on routing rules.

### FR-4: Controller shall support outbound WebSocket client connections to vComponents

The controller shall connect to each vComponent over WebSocket as a client to deliver configuration and control messages.

Acceptance criteria:
1. The controller can establish outbound WebSocket client connections to vComponent endpoints specified by configuration/profile.
2. Outbound message delivery to vComponents is asynchronous from the vComponent’s perspective (messages are received asynchronously as described in the HLD).

### FR-5: Controller shall maintain a vComponent endpoint registry

The controller shall maintain a list of ports and WebSocket addresses for vComponent communication.

Acceptance criteria:
1. The endpoint registry can represent the configured vComponents and their endpoints in a deterministic manner at runtime.
2. The endpoint registry is created and available before control-plane traffic is routed.

### FR-6: Controller shall support YAML message payloads with pre-defined formats

The controller shall support control plane messages expressed in YAML format, with pre-defined message structures for supported interactions.

Acceptance criteria:
1. Incoming YAML messages are validated at least to the extent needed for routing and forwarding decisions.
2. If a message is malformed or unsupported, it is rejected or ignored in a controlled manner with diagnostic logging.

### FR-7: Controller and vComponents shall implement KVP-based routing semantics

The controller shall route messages based on KVP semantics, where routing is determined by the presence of keys in the message payload rather than by control-plane port assignment.

Acceptance criteria:
1. For a given message payload, the destination vComponent(s) is determined by key presence rules rather than by per-component port registration.
2. Unknown keys or unsupported message types do not crash the controller; they result in an explicit log and a controlled handling policy.

### FR-8: Controller shall configure vComponents using a device profile artifact (HPF/HFP)

The controller shall configure each vComponent using the device profile artifact provided at boot.

Acceptance criteria:
1. The controller loads and applies the selected HPF/HFP profile from a local path provided by boot/kernel configuration (MVP scope).
2. The controller can use the profile to determine which vComponents are active and how they are configured.

### FR-9: MVP shall support configuration for at least one STB profile and one Panel profile

The MVP shall support configuring the vDevice and its vComponents for at least one STB and one Panel profile.

Acceptance criteria:
1. The controller can successfully apply each of the two profiles and reach a stable initialized state.
2. Control plane messages can be routed and handled for each profile in a repeatable manner.

## Non-functional requirements

### NFR-1: Host independence within the MVP target envelope

The implementation shall be host-independent in the sense described by the HLD: it must run locally on PCs (Linux host) in a VM without requiring physical device dependencies.

Acceptance criteria:
1. The vDevice controller can operate in a QEMU-hosted environment on Linux using the MVP-defined configuration mechanisms.

### NFR-2: Logging and observability alignment with RDKE logging infrastructure

The controller shall comply with RDKE logging infrastructure requirements and provide enough logs to debug profile selection, boot sequence milestones, endpoint registry initialization, and message routing decisions.

Acceptance criteria:
1. Logs exist for controller start, profile load, service manager readiness integration points, control plane message receipt, routing decisions, and forwarding outcome.
2. Logs provide enough detail to diagnose failures in profile load and message routing without requiring invasive instrumentation.

### NFR-3: Build quality expectations for controller-owned code

Controller-owned code shall meet the HLD acceptance criterion of “zero compilation warnings in the developed code” and align to the coding guidelines referenced by the HLD.

Acceptance criteria:
1. Controller-owned compilation completes without warnings under the project’s configured toolchain.

### NFR-4: CI suitability and deterministic behavior

The controller should support deterministic behavior suitable for CI/CD and test farms, including deterministic profile selection and stable initialization milestones.

Acceptance criteria:
1. Given the same profile and inputs, initialization behavior is reproducible in timing-independent observable ways (milestone logs and routing behavior).
2. Profile selection is automatable (e.g., via boot parameters or equivalent configuration injection).

## System context and conceptual architecture

The HLD describes a layered system where the vDevice image is a normal RDK-E image booted as a VM. The Apps and Middleware layers are unchanged versus physical targets. The vendor layer is replaced with a “Vendor AIDL simulation” implementation composed of vComponents. Within this environment, the vDevice Controller is the framework component responsible for configuration and message routing.

The HPK Control Plane documentation emphasizes unified message handling using a common YAML message structure, with the ControlPlaneClass determining the target (virtual device versus physical rack). In a vDevice setup, the control message is sent through WebSocket and processed by the vDevice Controller, which forwards to the appropriate vComponent.

### Boot and initialization sequence (product view)

Based on the HLD execution flow and the mapping notes, the product-level boot sequence expectations are as follows.

First, U-Boot passes the Hardware Profile (HPF) YAML configuration file to the kernel, using a local device path for MVP. Second, the Service Manager is up and running as part of the boot sequence, and vComponents register with the Service Manager for Binder IPC communication. Third, the vDevice Controller loads the HPF and configures each vComponent. Fourth, the controller establishes and maintains the endpoint registry and connections required for routing. Finally, VTS tests vComponents independently, using vDevice-specific configuration where applicable.

## Acceptance criteria (overall)

The VController/Controller PRD is considered satisfied when the following are true.

First, the controller can configure and start a vDevice runtime based on a boot-provided HPF/HFP profile and can support at least one STB profile and one Panel profile for MVP. Second, external control-plane stimulus can deliver YAML messages in a vDevice setup and those messages are routed through the controller and forwarded to the appropriate vComponents. Third, the controller’s connectivity model is implementable and documented: it handles the control plane socket and connects to vComponents over WebSocket as a client while maintaining a registry of vComponent endpoints. Fourth, message routing semantics are key-based (KVP) rather than port-based, and malformed or unknown messages are handled safely with diagnostic logging. Fifth, controller-owned code quality, determinism, and logging align with the HLD’s acceptance criteria and non-functional expectations.

## MVP scope definition

The MVP scope includes unified routing through the controller, profile-driven configuration for at least one STB and one Panel, explicit endpoint registry responsibilities, and YAML message handling with KVP-based routing semantics. The MVP excludes cloud profile retrieval, secure AV pipeline support, proprietary Dolby feature support, verification of VSI components (Bluez/Wi-Fi/OpenGL), and DMIPS performance monitoring tooling (moved beyond Phase 1 per the HLD).

## Risks, open questions, and decisions

### Terminology alignment risk: HPF/HFP vs platform profile naming

The HLD uses “HPF” (and also lists “HFP” as an acronym entry) for the device profile, while existing controller documentation may refer to `platformProfile.yaml`. This mismatch impacts both documentation and interfaces. A product-level decision is required on whether these are the same artifact under different names or distinct artifacts with a defined relationship.

### Control plane and controller transport topology clarity

The HLD states the controller “handles the control plane socket” and “connects to vComponent over Websocket as client.” HPK Control Plane documentation states that for vDevices the Control Plane exposes a WebSocket server for external stimulus. The product definition must ensure that the expected network topology and responsibilities are consistent and that the controller’s ingress and egress roles are explicit.

### Control message schema and routing keys

The HLD states messages are pre-defined and routing is KVP-based, but does not enumerate the exact key schema. Without a stable schema, interoperability will be inconsistent between control plane tooling, the controller, and vComponents. Defining and versioning the schema is a critical follow-up decision area.

### Schedule and dependency risks from VTS and Comcast-provided assets

The HLD explicitly calls out risks that issues in VTS test suites and delayed availability of control plane messages or vComponents may impact milestone timelines. The PRD assumes the required control message definitions and test assets are available as dependencies.

## References (code and documents)

This PRD is grounded in the following repository artifacts and ingested documents.

1. Attached HLD text: `attachments/20260109_082552_Requirement_HighLevelDesign for Virtual_Design V0.0.3_Comments_updated[58](docx).txt`.
2. HLD-to-design mapping notes: `rdk-halif-aidl-17/docs/virtual_device/vcontroller_design_update_mapping.md`.
3. HPK Control Plane documentation excerpt (ingested): “Control Plane - RDK Hardware Porting Kit” (describes WebSocket interface for vDevices and routing via vDevice Controller).
