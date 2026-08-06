---
title: Data files
layout: home
nav_order: 1
parent: Setup C-Slice
---

# Data files

C-Slice uses editable data files for cluster configurations, encoder parameters, color schemes, icons, and audio channel layouts.

In a source checkout, the files are stored under `data`:

```text
data/
  configs/
  parameters/
  audio-channel-layouts.json
  color-schemes/
  images/
```

In an installed build, the important runtime files are copied next to the executable:

```text
bin/
  C-Slice.exe
  data/
    configs/
    parameters/
    audio-channel-layouts.json
    color-schemes/
```

## Configuration files

`data/configs/*.json` contains SGCT configurations. C-Slice reads the `nodes` and `windows` entries from the selected file and lists every renderable window in the **Outputs** section.

## Parameter files

`data/parameters/*.json` contains optional FFmpeg parameter presets. These files can add codec options, pixel formats, or video filters beyond the main UI controls.

## Audio channel layouts

`data/audio-channel-layouts.json` defines the named layouts shown in **Tools > Audio Muxer**. Edit this file to rename layouts or adapt channel labels for a venue.