---
title: 'Slice workflow'
sidebar_label: 'Overview'
sidebar_position: 1
---

# Slice workflow

The main C-Slice UI is organized around the order of a production slice: choose inputs, choose outputs, set mapping and encoding, verify, then start the node process.

Detailed workflow guides:

- [Basic slice](/workflow/basic-slice)
- [Verify and preview](/workflow/verify-preview)
- [Job queue](/workflow/job-queue)
- [Progress, command preview, and logs](/workflow/monitoring)

## Toolbar actions

- **Start** launches node mode with the current settings.
- **Verify** checks selected image files before rendering. This applies to image sequence input only.
- **Pause** and **Resume** control a running node process.
- **Abort** stops the running node process.
- **Preferences** opens startup default settings.
- **Image Sequence Preview** opens a preview window for the selected input sequence. When **Input type** is set to *Video*, the button label changes to **Video Preview** and opens a video preview window instead.

The **Command preview** panel shows the node-mode command that will be launched. This is useful when troubleshooting a failed job or when comparing two machines.