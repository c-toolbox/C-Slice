---
title: Progress, command preview, and logs
layout: home
nav_order: 3
parent: Slice workflow
---

# Progress, command preview, and logs

C-Slice exposes the node-mode command, runtime progress, and log output in the main window.

## Progress

The **Progress** panel shows loaded and rendered frame counts, percentages, elapsed time, and estimated remaining time. During verification, the same area reports checked files.

If critical errors occur, the panel shows failed files and points you to the log for details.

## Command preview

The **Command preview** panel shows the command line that will be launched when you press **Start**. It includes the selected SGCT configuration, input paths, output settings, mapping options, and encoder arguments.

Use command preview to:

- Compare settings between machines.
- Confirm that a parameter JSON file is included.
- Check which SGCT window names will be rendered.
- Share exact run settings when reporting a problem.

## Log

The **Log** panel captures application and node-process messages. Use **Tools > Clear Log** when starting a new investigation so new output is easy to read.

When a job fails, the most useful information is usually near the first error in the log, not only the final line.