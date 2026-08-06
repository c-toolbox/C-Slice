---
title: Video input
layout: home
nav_order: 2
parent: Media and data structure
---

# Video input

C-Slice can use a video file as input instead of a numbered still image sequence. The rendering and output pipeline is identical — each selected SGCT window is still encoded into its own subfolder — but the source frames are decoded from a video file using MPV.

## Switching to video input

In the **Input sequence** panel, change **Input type** from *Image sequence* to *Video*. The panel label changes to **Video input** and the file dialogs switch to video file filters.

## Supported formats

The file browser filters for:

```text
*.mp4  *.mov  *.mkv  *.avi  *.webm  *.m4v  *.mpg  *.mpeg
```

Any format that MPV can decode is accepted. The filter is a convenience; you can type a path directly into the **Left/input** field.

## Reading metadata

After choosing a video file, press **Read metadata**. C-Slice probes the file with MPV and populates:

- **Start** — set to `0`
- **Stop** — set to the last frame index derived from duration and FPS
- Duration, FPS, and resolution shown in the status line

Use the reset buttons next to **Start** and **Stop** to restore the metadata-derived range after manual edits.

## Frame range and step

**Start**, **Stop**, and **Step** work the same as for image sequences:

- **Start** — first frame to process (0-based frame index).
- **Stop** — last frame to process.
- **Step** — skip frames when greater than `1`. A step of `2` processes every other frame.

A short test range (for example, 10 frames) is recommended before a full render.

## Stereo video input

Enable **Use right-eye input** to supply a separate right-eye video file. The two files must match in frame count, FPS, and resolution. Mismatches produce a warning in the sequence status line.

Example:

```text
show_left.mp4
show_right.mp4
```

## Video decoding mode

The **Video decoding mode** control is in the **Performance** section of the main settings panel. It sets how MPV decodes each input video:

| Mode | Description |
|------|-------------|
| **Software** | CPU decoding. Compatible with all systems. Slower on high-resolution or high-bitrate sources. |
| **Hardware** | GPU-accelerated decoding. Faster on supported hardware. Requires a compatible GPU and driver. |
| **Hybrid** | Hardware decoding for the left-eye video, software decoding for the right-eye video. Useful when the GPU cannot handle two concurrent hardware-decode streams in stereo mode. |

Start with **Hardware** on a GPU workstation. Fall back to **Software** if you see decoding artifacts or errors in the log. Use **Hybrid** for stereo when hardware decode is available but causes issues with dual-stream playback.

## Video Preview

Press the **Video Preview** button to open a preview window for the loaded video. Use it to verify the correct file is selected and that the expected content is visible before starting a render.

## Differences from image sequence mode

| | Image sequence | Video input |
|---|---|---|
| Input file | One image from the sequence | The video file itself |
| Index / metadata | **Re-index** button scans the sequence | **Read metadata** button probes the file |
| Frame range source | Numbered files on disk | Duration × FPS from video metadata |
| Image error behavior | Configurable (abort / pause / continue) | Not applicable |
| Preview | Image Sequence Preview (frame stepping) | Video Preview (MPV playback) |
