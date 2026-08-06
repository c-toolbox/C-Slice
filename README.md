# C-Slice : Media Slicer for Immersive Environments

C-Slice is an open source cluster media encoder, useful for video slicing to be played in immersive environments, through for instance C-Play, onto domes and powerwalls.

# Documentation

https://c-toolbox.github.io/C-Slice/

It is intended for producing slice dome, fisheye, spherical, planar, and SGCT-window based outputs from image sequences so the rendered result can be encoded per output window.

C-Slice starts in a master UI mode by default. When you press **Start**, the UI launches the same executable again in node mode with SGCT and slice-compatible command-line arguments.

## Build C-Slice

If C-Slice is built with *Wuffs* support, C-Slice will also use *Wuffs* for image loading:

*Wuffs* is optional, but highly recommended. Without it, C-Slice falls back to the bundled SGCT image loader.

## Data files

C-Slice uses editable data under `data/slice`:

- `configs/*.json` contains SGCT cluster/window configurations for slicing.
- `parameters/*.json` contains FFmpeg encoder parameter presets.
- `audio-channel-layouts.json` contains the Audio Muxer channel layout definitions.

Installed builds should keep the same structure next to the executable, for example:

```text
bin/
  C-Slice.exe
  data/
    slice/
      configs/
      parameters/
      audio-channel-layouts.json
```

## Basic workflow

1. Open **C-Slice**.
2. Choose the left/input image sequence.
3. Optionally choose a right-eye image sequence for stereo output.
4. Choose an output directory and base output name.
5. Choose an SGCT configuration from `data/slice/configs`.
6. Select the SGCT window outputs to render.
7. Choose mapping, encoding, and advanced options.
8. Press **Start**.

Each enabled SGCT window is written to its own output subdirectory named after the SGCT window, for example `Node1`, `Node2`, and so on.

## Input image sequences

When you choose an input image, C-Slice scans the surrounding numbered sequence and updates the start and stop frame range. For best results, use consistently numbered files such as:

```text
show_left_000000.png
show_left_000001.png
show_left_000002.png
```

The **Image Threads** setting controls how many image loading jobs C-Slice can run in parallel.

## SGCT configuration

The SGCT configuration determines the output windows that C-Slice can render. C-Slice reads the `windows` entries in the selected JSON configuration and shows them as selectable outputs in the UI.

Output command-line arguments use the SGCT window name instead of a generated channel name, making it easier to match output folders to the cluster configuration.

## Encoding

C-Slice uses FFmpeg for encoding. The **Encoding** section controls codec, preset, pixel rate, constant quality, frame rate, container preference, and optional parameter JSON.

Parameter presets are stored in `data/slice/parameters` and can be adjusted or replaced for local production needs.

## Audio Muxer

The **Tools → Audio Muxer** dialog can combine mono WAV channel files into one multi-channel WAV file. It supports the layouts defined in `data/slice/audio-channel-layouts.json`.

The layout file can be edited to rename layouts or change channel labels. Each layout has this structure:

```json
{
  "name": "Nrkp Dome",
  "channels": [
    "01: Front left",
    "02: Front right",
    "03: Front center"
  ]
}
```

Unchecked Audio Muxer channels are filled with silence in the FFmpeg filter graph. The global volume and per-channel gain are multiplied before muxing.

## Preferences

Use **Tools → Preferences** to save startup defaults for configuration files, mapping, encoder settings, thread counts, output container preference, and initial file dialog locations.

## Troubleshooting

- If no outputs are listed, check that the selected SGCT JSON file contains `nodes` with `windows` entries.
- If muxing audio fails, make sure `ffmpeg` or `ffmpeg.exe` is available next to C-Slice or on the system path.
- If image sequence scanning reports missing frames, check the numbering in the input directory.
- If output folders are not created, verify write permissions for the selected output directory.