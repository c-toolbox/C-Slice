---
title: Build from code
layout: home
has_children: true
has_toc: false
nav_order: 7
---

# Build C-Slice from source code

C-Slice is currently tested primarily on Windows. The build uses CMake, Visual Studio 2022, Qt 6, KDE Frameworks 6, SGCT, FFmpeg, Wuffs, and selected vcpkg dependencies.

These are the build guides:

- [Dependencies](guides/build/dependencies)
- [Configure and build with CMake](guides/build/cmake)
- [Package and deploy](guides/build/packaging)

## Build overview

1. Clone C-Slice with its `src/sgct` submodule available.
1. Install Qt 6 and KDE Frameworks 6, normally through KDE Craft on Windows.
1. Install vcpkg and set `VCPKG_ROOT`.
1. Install the vcpkg packages used by SGCT and C-Slice.
1. Build or install FFmpeg development libraries and make them discoverable by CMake.
1. Install or clone Wuffs and set `CSLICE_WUFFS_ROOT`.
1. Configure the project with CMake 3.25 or newer.
1. Build `C-Slice` and optionally the `INSTALL` target.

The default install prefix is a sibling `install` folder next to the source checkout.