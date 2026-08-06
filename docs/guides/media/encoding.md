---
title: Encoding outputs
layout: home
nav_order: 4
parent: Media and data structure
---

# Encoding outputs

C-Slice uses FFmpeg for output encoding. Each selected SGCT window is encoded independently, so the total encoder load depends on frame size, output count, codec, and quality settings.

## Codecs

The UI exposes these codec choices:

- MPEG-1, MPEG-2, MPEG-4
- H264, H265
- H264 NVENC, H265 NVENC
- VP8, VP9
- FFV1 (lossless)
- Hap
- ProRes
- PNG, JPEG, TGA

Movie codecs write video containers. PNG, JPEG, and TGA write still-image sequences. FFV1 outputs to Matroska (.mkv) containers.

## Frame rate and quality

Movie codecs use the selected frame rate. The UI provides 30, 60, and custom numerator/denominator controls.

Quality controls depend on codec:

- H.264 and H.265 software codecs use **CRF**.
- H.264 NVENC and H.265 NVENC use **CQ**.
- Other quality-based codecs use **QScale**.
- Some codecs expose **Pixel rate** instead of quality.

## Presets and tune

Software H.264/H.265 exposes FFmpeg/libx preset and tune values. NVENC exposes NVIDIA preset levels and tune modes. Slower presets usually improve compression efficiency but increase encoding time.

## Parameter JSON

Optional parameter files in `data/parameters` can add lower-level FFmpeg options. A parameter file can contain codec options, pixel formats, and video filters:

```json
{
  "codecParameters": {
    "options": [
      { "name": "tune", "value": "fastdecode" }
    ],
    "pixelFormats": [],
    "videoFilters": []
  }
}
```

Use parameter JSON files for production presets that should be reused across jobs.