---
title: 'Setup C-Slice'
sidebar_label: 'Overview'
sidebar_position: 1
---

# Setup C-Slice

C-Slice setup is mostly about matching three things: the SGCT configuration used by the target display system, the image sequence format exported from production, and the output encoding settings expected by playback.

Start with these guides:

- [Data files](/setup/data-files)
- [SGCT configuration](/setup/sgct-configuration)
- [Production workstation setup](/setup/workstation)

## Setup model

C-Slice does not launch a live playback cluster. It runs as a desktop production tool, then starts its own node-mode process to render the selected SGCT windows locally. The output files can then be copied to playback machines or loaded into a player such as C-Play.

For best repeatability, keep the same data files with the same relative paths across all machines used in the production pipeline.