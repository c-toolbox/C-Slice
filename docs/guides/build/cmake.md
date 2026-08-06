---
title: Configure and build with CMake
layout: home
nav_order: 2
parent: Build from code
---

# Configure and build with CMake

This guide describes the normal Windows CMake workflow.

## Environment

Before configuring, make sure the following are available in the environment or CMake cache:

- Qt and KDE Frameworks CMake package paths from Craft.
- `VCPKG_ROOT` pointing to the vcpkg checkout.
- FFmpeg include and library paths discoverable by `FindFFmpeg.cmake`.
- `CSLICE_WUFFS_ROOT` pointing to the Wuffs checkout.

## Important CMake options

- `BUILD_WITH_VCPKG_SUPPORT` defaults to `ON` and configures the vcpkg toolchain.
- `BUILD_CSLICE_WITH_WUFFS` defaults to `ON`.
- `CSLICE_WUFFS_ROOT` points to the Wuffs source checkout.
- `CSLICE_INSTALL_DEPENDENCIES` copies runtime dependencies and data files during install when enabled.
- `CSLICE_INSTALL_BUILD_BY_DEFAULT` includes the `INSTALL` target in the default Visual Studio build when enabled.

## Configure

Use CMake GUI, CMake Presets if added later, Visual Studio CMake integration, Qt Creator, or VS Code CMake Tools. The generated project target is `C-Slice`.

The default install prefix is set to a sibling `install` folder beside the source checkout:

```text
C-Slice/
install/
```

## Build

Build the `C-Slice` target first. If you want a runnable installed folder, enable dependency installation options and build the `INSTALL` target.

For development, run the executable from its build output directory with the required runtime DLL paths available. For production testing, prefer the installed folder because it includes copied data files and dependencies.