---
title: Package and deploy
layout: home
nav_order: 3
parent: Build from code
---

# Package and deploy

C-Slice includes install and packaging helpers for creating a runnable Windows folder.

## Install target

Enable `CSLICE_INSTALL_DEPENDENCIES` when configuring if you want the install step to copy runtime dependencies and C-Slice data files.

The install step can copy:

- `C-Slice.exe`
- Qt, KDE, FFmpeg, vcpkg, and optional SGCT runtime DLLs
- Qt/KDE plugins and QML files
- `data/configs`
- `data/parameters`
- `data/audio-channel-layouts.json`
- color schemes, icons, and log folder
- generated packaging scripts
- `LICENSE.txt`

## Dependency copy modes

The project has two dependency-copy strategies:

- `CSLICE_INSTALL_GET_RUNTIME_DEPENDENCIES` uses CMake runtime dependency scanning.
- `CSLICE_INSTALL_DLLS_FROM_PATHS` copies DLLs from configured directories.

On Windows 11 and newer, the project defaults toward copying DLLs from paths. On Windows 10 and older, it defaults toward runtime dependency scanning.

## Generated package helpers

CMake configures these helper files from templates:

- `CSLICE_Pack_Installer.nsi`
- `CSLICE_Pack_Zip.bat`

They are installed to the install prefix and can be used as the starting point for installer or zip packaging.

## Deployment check

After packaging, test from the installed `bin` folder on a clean machine or a clean user account. Confirm that C-Slice launches, the default data files appear, image preview works, a short slice can run, and the Audio Muxer can find FFmpeg.
