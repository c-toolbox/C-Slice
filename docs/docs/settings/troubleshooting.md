---
title: 'Troubleshooting'
sidebar_position: 3
---

# Troubleshooting

This page collects common C-Slice setup and runtime problems.

## No outputs are listed

Check that the selected SGCT JSON file contains `nodes` with `windows` entries. Also confirm that the file can be opened from the path shown in the **SGCT config** field.

## Image sequence range is wrong

Use consistently numbered files and select an image from the intended sequence. If left and right ranges differ, check missing files and naming conventions.

## Verify reports failed files

Open the failed files from the Progress panel. Re-export or replace damaged images, then run **Verify** again.

## Audio muxing fails

Make sure `ffmpeg.exe` is next to C-Slice or available on `PATH`. Also confirm that checked channels have input files and that the output WAV path is writable.

## Output folders are not created

Check write permissions and available disk space in the selected output directory. Avoid protected system folders for production output.

## NVENC encoding fails

Confirm that the machine has a compatible NVIDIA GPU and driver. If the job must continue on another machine, switch to a software codec or still-image output.

## Render speed is unstable

Run a short benchmark with **Run without encoding**. If that is stable, the encoder is the bottleneck. If it is still unstable, tune loading threads, GPU slots, disk location, and memory usage.

## Slicing slows down or stops

Each run writes everything the node prints to `<output directory>/logs/cslice_node_<kind>_<date>_<time>.log`. The **Node log** line in the Progress panel shows the exact path. Look for these lines:

- `Throughput rendered … frames/s | LoadThroughput … MB/s … read avg … ms … decode avg … ms`: printed every 10 seconds. If read time is far above decode time and MB/s is flat (for example around 250 MB/s), the input storage or network is the bottleneck. Heavy frames simply take longer, so copy the sequence to local NVMe for faster runs.
- `SlowRead` / `SlowDecode`: a single image took more than 10 seconds to read or decode. The line names the worker, the frame and the file.
- `DecodeFailed`: an image could not be decoded. The line includes the reason.
- `Stall no rendered frame for …s`: printed when no frame has been rendered for 30 seconds, followed by `Stall LoaderState/LoaderSource/LoaderSlot` lines. They show what the render thread is waiting for (input frame, readback slot or encoder) and, for each loader slot, which file it has been working on and for how long. A slot stuck in `Decoding` for minutes on one file usually means a hung network read.

Set the environment variable `CSLICE_STALL_SECONDS` to change the 30-second stall threshold.

## Video input

### Metadata does not populate the frame range

Press **Read metadata** after choosing a video file. If the fields remain empty, check that MPV can open the file by opening it in the **Video Preview** window. Confirm the file path is accessible and not locked by another process.

### Decoding artifacts or wrong frames

Switch **Video decoding mode** from *Hardware* to *Software* in the Performance section. Some GPUs or drivers produce artifacts with hardware-accelerated decoding for specific codecs or resolutions.

### Stereo hardware decoding fails

If hardware decoding works for mono but not for stereo, set **Video decoding mode** to *Hybrid*. This uses hardware for the left eye and software for the right eye, which avoids the dual-stream constraint on some GPUs.

### Video preview does not open

Confirm that MPV is available to C-Slice. In packaged builds, MPV libraries should be included. Check the log for missing library errors.