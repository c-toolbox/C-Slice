---
title: Media and data structure
layout: home
has_children: true
has_toc: false
nav_order: 4
---

# Media and data structure

C-Slice reads still image sequences or video files and writes one output per selected SGCT window. Depending on the selected codec, an output can be a movie file or a numbered still-image sequence.

Use these guides for the details:

- [Image sequences](guides/media/image_sequences)
- [Video input](guides/media/video_input)
- [Mapping modes](guides/media/mapping)
- [Encoding outputs](guides/media/encoding)
- [Audio Muxer](guides/media/audio_muxer)

## Media structure

A typical production slice from an image sequence starts from a source folder such as:

```text
source/
  show_left_000000.png
  show_left_000001.png
  show_left_000002.png
```

Stereo work normally uses a matching right-eye sequence:

```text
source/
  show_right_000000.png
  show_right_000001.png
  show_right_000002.png
```

For video input, a single file (or a left/right pair for stereo) is used instead:

```text
source/
  show_left.mp4
  show_right.mp4
```

C-Slice writes each enabled SGCT window into its own output folder named after that window. For example:

```text
output/
  DomeMaster/
    show_DomeMaster.mp4
  ProjectorLeft/
    show_ProjectorLeft.mp4
  ProjectorRight/
    show_ProjectorRight.mp4
```

The exact window names come from the selected SGCT JSON configuration.