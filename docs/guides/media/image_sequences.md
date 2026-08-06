---
title: Image sequences
layout: home
nav_order: 1
parent: Media and data structure
---

# Image sequences

C-Slice opens numbered still image sequences. When you choose one input image, C-Slice scans the surrounding numbered files and updates the **Start** and **Stop** frame range.

Use consistent numbering:

```text
show_left_000000.png
show_left_000001.png
show_left_000002.png
```

Supported input filters in the UI include PNG, JPEG, and TGA files. Current Wuffs-enabled builds provide fast image loading for common production formats.

## Stereo sequences

Enable **Use right-eye input** for stereo work. The right-eye sequence should match the left-eye sequence in frame count, numbering, dimensions, and file availability.

Example:

```text
show_left_000120.png
show_right_000120.png
```

## Frame range and step

- **Start** is the first source frame to process.
- **Stop** is the last source frame to process.
- **Step** skips frames when greater than `1`.

Use **Verify** to decode-check the selected files before a long render.

## Upside down

Enable **Upside down** if the source sequence needs to be flipped for the target rendering setup. This is useful when content was exported with an orientation different from the SGCT projection assumptions.

## See also

- [Video input](video_input) — for using video files as input instead of still image sequences.