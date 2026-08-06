---
title: Slice workflow
layout: home
has_children: true
has_toc: false
nav_order: 5
---

# Slice workflow

The main C-Slice UI is organized around the order of a production slice: choose inputs, choose outputs, set mapping and encoding, verify, then start the node process.

Detailed workflow guides:

- [Basic slice](guides/workflow/basic_slice)
- [Verify and preview](guides/workflow/verify_preview)
- [Job queue](guides/workflow/job_queue)
- [Progress, command preview, and logs](guides/workflow/monitoring)

## Toolbar actions

- **Start** launches node mode with the current settings.
- **Verify** checks selected image files before rendering. This applies to image sequence input only.
- **Pause** and **Resume** control a running node process.
- **Abort** stops the running node process.
- **Preferences** opens startup default settings.
- **Image Sequence Preview** opens a preview window for the selected input sequence. When **Input type** is set to *Video*, the button label changes to **Video Preview** and opens a video preview window instead.

The **Command preview** panel shows the node-mode command that will be launched. This is useful when troubleshooting a failed job or when comparing two machines.