---
title: Settings
layout: home
has_children: true
has_toc: false
nav_order: 6
---

# The settings within C-Slice

C-Slice has two levels of settings. The main window controls the current slice job, while **Tools > Preferences** stores startup defaults for future jobs.

Settings guides:

- [Preferences](guides/settings/preferences)
- [Performance settings](guides/settings/performance)
- [Troubleshooting](guides/settings/troubleshooting)

## Current job settings

Current job settings include input paths, output directory, SGCT configuration, selected output windows, mapping mode, codec, encoder quality, and advanced thread or GPU slot counts.

These settings are reflected in the **Command preview** panel before the node process starts.

## Startup defaults

Preferences are saved in the user configuration file `C-Slice/CSLICE.conf`. Defaults include SGCT configuration paths, first file dialog locations, mapping defaults, encoder defaults, and advanced performance settings.