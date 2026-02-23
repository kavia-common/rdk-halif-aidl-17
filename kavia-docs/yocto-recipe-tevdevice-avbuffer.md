<!--
MongoDB Document Metadata:
- Original File Path: kavia-docs/CodeWiki/Specs/DetailedDesigns/yocto-recipe-tevdevice-avbuffer.md
- Operation: write
- Timestamp: 2026-02-19T13:05:27.285164+00:00
- Restored At: 2026-02-23T05:04:11.254447+00:00
- Task ID: cm219d4578
-->

# TEVDevice AVBuffer Yocto Recipe (Derived from build.sh and CMake)

## Purpose

This document derives a practical Yocto/BitBake recipe for the TEVDevice repository under `10002556%2FTEVDevice/`. The intent is to translate the repository’s current “developer build” flow (`build.sh`) and its CMake build/install model (`CMakeLists.txt`) into a reproducible Yocto package suitable for the RDK-E “opkg per component” approach.

The TEVDevice codebase in this workspace is specifically building an AVBuffer Binder service and its supporting shared libraries. The primary installed runtime artifact is the `RDKAVBufferService` executable, which is started with a path to a HAL Feature Profile (HFP) YAML file.

## What the project builds and installs (from CMakeLists.txt)

The top-level CMake project is declared as:

- `project(AVBuffer LANGUAGES CXX)`
- C++ standard is C++17.

The build produces:

- `libavbuffer_core.so` (shared library)
- `libavbuffer_helper.so` (shared library)
- `RDKAVBufferService` (executable)
- Optionally, `RDKAVBufferService_test` (executable), controlled by `BUILD_TESTS` (defaults to `ON` in CMake, but should typically be disabled in Yocto builds unless you are packaging tests).

The build also installs a configuration YAML:

- Source: `vcomponent_configurations/hfp-avbuffer.yaml`
- Install destination selected by the CMake file is:
  - `${CMAKE_INSTALL_DATADIR}/avbuffer/hfp-avbuffer.yaml` which typically resolves to `/usr/share/avbuffer/hfp-avbuffer.yaml` when installing under `/usr`.

The service entrypoint in `src/service/vcomponent_BufferService.cpp` expects the YAML path as argv[1] and fails fast if the file is missing or not readable.

## Dependencies implied by the code and build files

### Linux Binder runtime (service manager, headers, libraries)

`build.sh` stages linux-binder outputs (tools + libs + headers) into a local prefix (`build/usr`) and then builds against those.

The CMake code explicitly links `RDKAVBufferService` to:

- `binder` (e.g., `libbinder.so`)
- `utils` (e.g., `libutils.so`)
- `log` (e.g., `liblog.so`)

It also includes Binder headers in `vcomponent_AvBufferManager.h`:

- `<binder/BinderService.h>`
- `<binder/Status.h>`
- `<utils/...>`

In Yocto terms, this means the recipe must have build-time access to the Binder headers and libraries in the target sysroot. Depending on your layer naming, this is commonly modeled as a dependency on a `linux-binder-idl` or similar provider that installs the Binder runtime.

### HALIF AIDL generated C++ headers and a `libhal_aidl.so` library

The TEVDevice project relies on generated C++ AIDL artifacts, referenced in `vcomponent_AvBufferManager.h` as includes such as:

- `"com/rdk/hal/PropertyValue.h"`
- `"com/rdk/hal/avbuffer/BnAVBuffer.h"`
- and other generated `com/rdk/hal/...` headers.

The `CMakeLists.txt` chooses AIDL header location as follows:

- If `CMAKE_SYSROOT` is set, it uses `${CMAKE_SYSROOT}/usr/include/hal/h`.
- Otherwise it uses a developer build location `${PROJECT_SOURCE_DIR}/build/current/h/`.

It also requires a shared library named `hal_aidl` / `libhal_aidl.so`, located via `find_library()` in:

- `${CMAKE_SYSROOT}/usr/lib` (Yocto case), or
- `${PROJECT_SOURCE_DIR}/build/usr/lib` (developer build case).

`build.sh` generates the headers/sources by cloning `rdk-halif-aidl` and running its CMake-based generator for `avbuffer`, `audiodecoder`, and `videodecoder`. It then builds `libhal_aidl.so` from those generated `.cpp` sources via `aidl_lib/Makefile`.

In Yocto, you should normally model this as a separate recipe (or set of recipes) that produces:

1. Generated AIDL C++ headers installed under `/usr/include/hal/h/...`
2. The corresponding shared library `libhal_aidl.so` installed under `/usr/lib`

The TEVDevice recipe can then depend on that package.

### UT framework libraries and headers

The AVBuffer manager header includes:

- `<ut.h>`
- `<ut_log.h>`
- `<ut_kvp.h>`
- `<ut_control_plane.h>`

The TEVDevice `CMakeLists.txt` expects to link to a `ut_control` library (found as `libut_control.so`), and in the developer flow `build.sh` gets it by cloning and building `ut-core`.

In Yocto, this should be modeled as a dependency on whatever package provides:

- `libut_control.so` in the target sysroot
- `ut.h` and the other `ut_*` headers in the target sysroot include path

If your stack is splitting “ut-core” and “ut-control” across repositories, ensure the final packaged headers match what TEVDevice includes.

## Mapping build.sh to a Yocto build model

The repository’s `build.sh` is a developer convenience wrapper that performs three distinct build roles:

1. Preparing the Binder toolchain/runtime (linux-binder-idl) in a local prefix.
2. Generating + building HALIF AIDL C++ artifacts into `libhal_aidl.so`.
3. Building UT libraries (via `ut-core`), then building the TEVDevice service via CMake.

In Yocto, a clean separation is usually:

- Recipe A: linux-binder runtime (`libbinder`, `libutils`, `liblog`, `servicemanager`, headers)
- Recipe B: HALIF AIDL generation + `libhal_aidl.so` packaging
- Recipe C: UT libraries (`libut_control.so` and headers)
- Recipe D (this doc): TEVDevice AVBuffer service and its helper libraries

This separation matches the “opkg per component” theme described in the vDevice HLD (packages per component/layer) while keeping TEVDevice’s recipe focused on its own build.

## Proposed Yocto recipe (template)

The following is a template recipe that assumes the Binder runtime, UT libraries, and HALIF AIDL artifacts are already available in the Yocto sysroot via other recipes.

You will need to adapt the dependency names (`DEPENDS`) to match your layer’s recipe naming and package naming conventions.

### File: `tevdevice-avbuffer_git.bb` (template)

```bitbake
SUMMARY = "TEVDevice AVBuffer Binder service (RDKAVBufferService) and helper libraries"
DESCRIPTION = "Builds the TEVDevice AVBuffer service and its supporting shared libraries."
HOMEPAGE = "N/A"
LICENSE = "Apache-2.0"

# NOTE: Replace the md5 placeholders using:
#   md5sum LICENSE
#   md5sum NOTICE
LIC_FILES_CHKSUM = "\
    file://LICENSE;md5=<REPLACE_WITH_MD5_OF_LICENSE> \
    file://NOTICE;md5=<REPLACE_WITH_MD5_OF_NOTICE> \
"

# NOTE: Replace the repo URL and SRCREV with your actual source origin.
SRC_URI = "\
    git://<TEVDEVICE_GIT_URL>;protocol=https;branch=main \
    file://RDKAVBufferService.service \
"
SRCREV = "<REPLACE_WITH_COMMIT_SHA>"
S = "${WORKDIR}/git"

inherit cmake systemd

# The upstream CMake defaults BUILD_TESTS=ON. Yocto builds typically disable unit tests
# unless you have a test packaging strategy.
EXTRA_OECMAKE += " -DBUILD_TESTS=OFF "

# These are logical dependency categories inferred from CMakeLists.txt and headers:
# - Binder runtime libraries: libbinder, libutils, liblog
# - HALIF AIDL generated headers and libhal_aidl.so
# - UT framework libraries: libut_control.so + headers (ut.h, ut_log.h, ut_kvp.h, ut_control_plane.h)
DEPENDS += " \
    linux-binder-idl \
    hal-aidl \
    ut-core \
"

SYSTEMD_SERVICE:${PN} = "RDKAVBufferService.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/RDKAVBufferService.service ${D}${systemd_system_unitdir}/RDKAVBufferService.service
}

# The CMakeLists installs the HFP YAML to ${datadir}/avbuffer by default.
# Keep it in the runtime package explicitly.
FILES:${PN} += " \
    ${datadir}/avbuffer/hfp-avbuffer.yaml \
"
```

### File: `RDKAVBufferService.service` (template)

This unit wraps the requirement that the service is invoked with a readable HFP YAML path. It uses the location that the CMake install step selects (`/usr/share/avbuffer/hfp-avbuffer.yaml` under a `/usr` prefix).

```ini
[Unit]
Description=RDK AVBuffer Binder Service (TEVDevice)
After=network.target
# If your platform has a Binder service manager unit, add it here, for example:
# After=servicemanager.service
# Requires=servicemanager.service

[Service]
Type=simple
ExecStart=/usr/bin/RDKAVBufferService /usr/share/avbuffer/hfp-avbuffer.yaml
Restart=on-failure
RestartSec=2s

[Install]
WantedBy=multi-user.target
```

## Optional variant: vendor-layer layout (/vendor/<module>) and aggregation symlinks

The RDK-E filesystem guidance in `rdk-halif-aidl-17/docs/vsi/filesystem/current/directory_and_dynamic_linking_specification.md` describes a layer/module directory model such as `/vendor/<module>/bin`, `/vendor/<module>/lib`, and a layer aggregation directory like `/vendor/bin` and `/vendor/lib` (with symlinks).

If your image uses that model, you can adapt the recipe by:

1. Installing TEVDevice into a module-scoped prefix, for example `/vendor/avbuffer`.
2. Creating the layer aggregation symlinks in `/vendor/bin` and `/vendor/lib` that point into `/vendor/avbuffer/...`.

This can be achieved without changing TEVDevice’s CMake by overriding the install prefix and then adding a `do_install:append()` symlink stage. Conceptually:

- Add: `EXTRA_OECMAKE += " -DCMAKE_INSTALL_PREFIX=/vendor/avbuffer "`
- Then create:
  - `/vendor/bin/RDKAVBufferService -> /vendor/avbuffer/bin/RDKAVBufferService`
  - `/vendor/lib/libavbuffer_core.so.* -> /vendor/avbuffer/lib/...` and similarly for helper
  - Optionally provide `ld.so.conf.d` entries per the spec and aggregate them into `/vendor/ld.so.conf.d`.

This is intentionally not shown as a final recipe here because the exact “layer mount points” and service enablement policy can differ per platform image.

## Verification checklist for integration

A Yocto integrator can validate correct integration by confirming the following outcomes:

1. The package installs `RDKAVBufferService` and both shared libraries into the intended prefix and library path.
2. The HFP YAML is installed at `/usr/share/avbuffer/hfp-avbuffer.yaml` (or the vendor-layer equivalent).
3. The runtime image contains the Binder runtime libraries that TEVDevice links against (`libbinder.so`, `libutils.so`, `liblog.so`).
4. The runtime image contains the HALIF AIDL library (`libhal_aidl.so`) and the corresponding generated headers were present in sysroot at build time under `/usr/include/hal/h`.
5. The runtime image contains UT headers and the `libut_control.so` library.
6. Starting the systemd unit runs the service without a “file not readable” error (confirming the YAML path is correct).

## Notes and limitations

The recipe template above is intentionally conservative. It does not attempt to replicate `build.sh`’s cloning/building of `rdk-halif-aidl` or `ut-core` inside the TEVDevice recipe. In production Yocto layers, those should generally be separate recipes so that TEVDevice can consume stable artifacts via sysroot, rather than rebuilding them internally.

If your current Yocto layer does not yet provide `libhal_aidl.so` and the generated headers, you should first introduce a dedicated recipe for the HALIF AIDL generation/build step (mirroring the `generate_hal_interfaces` and `aidl_lib/Makefile` steps from `10002556%2FTEVDevice/build.sh`).
