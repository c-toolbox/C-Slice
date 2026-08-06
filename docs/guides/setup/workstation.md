---
title: Production workstation setup
layout: home
nav_order: 3
parent: Setup C-Slice
---

# Production workstation setup

C-Slice rendering and encoding can be heavy. A production workstation should be configured for predictable disk, GPU, memory, and encoder behavior before long slice jobs.

## Storage

Use fast local storage for both the input image sequence and the output directory. Avoid writing encoded outputs to a slow network share while rendering unless the network storage has been tested with the target frame size and number of output windows.

## GPU and driver

C-Slice uses OpenGL through SGCT for rendering and capture. Install current GPU drivers and confirm that the selected SGCT configuration opens correctly before starting a large batch.

NVENC codecs require a compatible NVIDIA GPU and driver. If NVENC is unavailable or fails, switch to software H.264, H.265, ProRes, Hap, or image-sequence output.

## FFmpeg process access

Keep FFmpeg runtime files with C-Slice. The Audio Muxer expects `ffmpeg.exe` next to the application or on `PATH`.

## Small test first

Before running thousands of frames, set a short **Start** and **Stop** range and render a few frames from every selected output window. Check file names, orientation, mapping, stereo mode, and playback compatibility.