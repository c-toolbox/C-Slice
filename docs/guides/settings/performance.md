---
title: Performance settings
layout: home
nav_order: 2
parent: Settings
---

# Performance settings

The **Advanced** section controls how much parallel work C-Slice schedules for loading, rendering, capture, and encoding.

## Encoder threads

**Encoder threads** is the maximum encoder thread count per output window. High values can help software codecs, but many selected windows can multiply the total CPU load quickly.

## Capture GPU slots

**Capture GPU Slots** controls GPU readback buffering per window. Increase it when capture stalls, but remember that more slots consume more GPU memory.

## Loading threads and memory

**Loading threads** controls how many image loading jobs can run in parallel. This applies to image sequence input. **Load Max CPU/RAM %** limits decoded image cache pressure.

If the machine starts paging memory to disk, reduce loading threads or the memory percentage.

## Loader GPU slots

**Loader GPU Slots** controls the texture upload ring buffer. Increase it when loading is fast but rendering waits for texture availability.

## Video decoding mode

When **Input type** is set to *Video*, the **Video decoding mode** control sets how MPV decodes the source file:

- **Software** — CPU decoding. Compatible with all systems. Use this as a fallback if hardware decoding causes artifacts or errors.
- **Hardware** — GPU-accelerated decoding. Faster on supported hardware. Requires a compatible GPU and driver.
- **Hybrid** — Hardware decoding for the left-eye video, software decoding for the right-eye video. Use this when the GPU cannot sustain two concurrent hardware-decode streams in stereo mode.

## Benchmark modes

Enable **Run without encoding** to test loading, rendering, capture, and readback without encoder cost.

Enable **Run without readback** while running without encoding to test loading and rendering only.

Use benchmark modes on short frame ranges when tuning a workstation.