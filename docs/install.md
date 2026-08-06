---
title: Install C-Slice
layout: home
nav_order: 2
---

# Installation instructions

C-Slice is currently documented for Windows production systems. Install the application in the same path on every workstation where you need to run or verify slice jobs.

## Getting the installer

A Windows installer is available through the [GitHub Releases page](https://github.com/c-toolbox/C-Slice/releases). Download the latest release package from that page to get started with installation.

If you receive C-Slice as an installer or zip package, keep the installed folder intact. The executable expects its runtime dependencies and editable data files to be available next to it.

## Installed folder layout

A packaged C-Slice build normally has this structure:

```text
install/
  bin/
    C-Slice.exe
    data/
      configs/
      parameters/
      audio-channel-layouts.json
      color-schemes/
      icons/
```

The important editable folders are:

- `data/configs` - SGCT JSON configurations that define the output windows.
- `data/parameters` - optional FFmpeg parameter JSON files.
- `data/audio-channel-layouts.json` - channel layout definitions for the Audio Muxer.

## First launch checklist

1. Start `C-Slice.exe`.
1. Open **Tools > Preferences** and confirm the default mono and stereo SGCT configurations.
1. Choose a left/input source: select one image from a small numbered sequence, or choose a video file after switching **Input type** to *Video*.
1. Choose an output directory with enough free space.
1. Press **Verify** before running the first full slice (image sequence input only).

## FFmpeg availability

C-Slice links directly to FFmpeg for encoding. Packaged builds should include the required FFmpeg runtime DLLs. The Audio Muxer also uses an FFmpeg process, so keep `ffmpeg.exe` in the installed `bin` folder or available on the system `PATH`.

## Next steps

After installation, continue with [Setup C-Slice](setup) and [Slice workflow](workflow).