---
title: SGCT configuration
layout: home
nav_order: 2
parent: Setup C-Slice
---

# SGCT configuration

C-Slice uses SGCT JSON configuration files to decide which output windows can be rendered. The same configuration family should normally be used by the playback system so the slice output matches the real display layout.

SGCT documentation is available here:

- [SGCT Docs](https://sgct.github.io/)
- [SGCT configuration files](https://sgct.readthedocs.io/en/latest/users/configuration/index.html)

## What C-Slice reads

C-Slice scans the selected JSON file for `nodes` and `windows`. Each window becomes a selectable output in the UI.

A minimal shape looks like this:

```json
{
  "version": 1,
  "masteraddress": "127.0.0.1",
  "nodes": [
    {
      "address": "127.0.0.1",
      "windows": [
        {
          "name": "DomeMaster",
          "size": { "x": 1024, "y": 1024 },
          "res": { "x": 4096, "y": 4096 },
          "viewports": []
        }
      ]
    }
  ]
}
```

The window `name` is used for output folder naming and command-line arguments. Use stable, production-friendly names such as `DomeMaster`, `Projector01`, or `LeftWall`.

## Warping and blend masks

The **Config options** checkboxes pass warping and blend-mask preferences to the node process. Enable them when the SGCT configuration and installed assets are intended to apply projector calibration data.

## Mono and stereo defaults

Preferences store separate default SGCT configurations for mono/2D and stereo/3D jobs. The default source values are:

- `configs/nrkp_mono_2026.json`
- `configs/nrkp_stereo_2026.json`

Use venue-specific files when preparing output for a particular dome, wall, or cluster.