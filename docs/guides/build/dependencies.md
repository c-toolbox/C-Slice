---
title: Dependencies
layout: home
nav_order: 1
parent: Build from code
---

# Dependencies

C-Slice is a C++23 CMake project. The current Windows build expects Visual Studio 2022 and recent dependency builds.

## Required tools

- Visual Studio 2022 with the C++ workload.
- CMake 3.25 or newer.
- Git with submodule support.
- KDE Craft for Qt 6 and KDE Frameworks 6 packages.
- vcpkg, with `VCPKG_ROOT` set in the environment.

## Qt and KDE Frameworks

C-Slice requires Qt 6.6 or newer and KDE Frameworks 6.0 or newer.

Required CMake packages include:

- Qt6Core, Qt6Gui, Qt6Qml, Qt6Quick, Qt6QuickControls2
- Extra CMake Modules
- KF6ColorScheme, KF6Config, KF6CoreAddons, KF6FileMetaData, KF6I18n, KF6IconThemes, KF6KIO, KF6Kirigami, KF6WindowSystem, KF6XmlGui
- Breeze is recommended for icons and styling

When using Craft on Windows, install the Qt/KF packages that provide those CMake packages before configuring C-Slice.

## vcpkg packages

The default CMake option `BUILD_WITH_VCPKG_SUPPORT` is enabled. With that option, CMake uses the toolchain at `%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake`.

Install the packages noted by the project configuration:

```text
vcpkg install minizip libpng tinyxml2
```

Use the same architecture triplet as the rest of the build, normally x64 on Windows.

## FFmpeg

C-Slice links directly against FFmpeg libraries and headers. Make sure CMake can find the FFmpeg include directory and libraries for the build configuration.

At runtime, keep the matching FFmpeg DLLs with `C-Slice.exe`. The Audio Muxer also needs `ffmpeg.exe` available next to the app or on `PATH`.

## Wuffs

Current C-Slice source builds expect a Wuffs checkout. Set `CSLICE_WUFFS_ROOT` to the Wuffs source root. CMake expects this file:

```text
CSLICE_WUFFS_ROOT/release/c/wuffs-unsupported-snapshot.c
```

Wuffs is used for high-performance image loading.