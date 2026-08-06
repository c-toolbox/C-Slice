---
title: Basic slice
layout: home
nav_order: 1
parent: Slice workflow
---

# Basic slice

This is the normal workflow for rendering a source into per-window outputs.

1. Open C-Slice.
1. In the **Input sequence** panel, confirm **Input type** is correct: *Image sequence* for numbered still images, or *Video* for a video file.
1. Choose **Left/input** and select the source: one image from the numbered sequence (image sequence mode) or the video file (video mode).
1. Enable **Use right-eye input** and choose a right-eye source if the job uses separate left and right inputs.
1. Confirm **Start**, **Stop**, and **Step**. For video input, press **Read metadata** first to populate the frame range from the file.
1. Choose an output **Directory** and **Base name**.
1. Choose the **SGCT config** for the target display setup.
1. Select the output windows to render.
1. Choose **Map onto**, stereo layout, radius, FOV, rotation, plane settings, or ROI as needed.
1. Choose codec, container, frame rate, quality, preset, tune, bit depth, and optional parameter JSON.
1. Press **Verify** (image sequence input only).
1. Press **Start**.

Each selected output window is written into its own subfolder named after the SGCT window.

## Short test render

Before a full render, set a small frame range such as 10 frames and check the output. This catches wrong window selection, flipped orientation, stereo layout mistakes, and incompatible encoder choices early.

## Opening outputs

Use **File > Open Output Directory** to open the selected output directory after a render.