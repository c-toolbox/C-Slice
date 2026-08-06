---
title: Mapping modes
layout: home
nav_order: 3
parent: Media and data structure
---

# Mapping modes

C-Slice maps the input image onto a virtual surface before capturing the SGCT windows. The selected SGCT configuration decides how the virtual surface is viewed by each output window.

## Available modes

- **Dome** maps the input onto a dome/fisheye surface. Use **Radius** and **FOV** to match the dome model.
- **Sphere EQR** maps equirectangular 360 content onto a sphere.
- **Sphere EAC** maps equiangular cubemap content onto a sphere.
- **Plane** maps flat media onto a planar surface in the scene.

## Stereo layout

The **Stereo** mapping control describes how stereo is stored in the selected input image:

- **2D (mono)** uses a single image view.
- **3D (side-by-side)** expects left and right views next to each other.
- **3D (top-bottom)** expects left and right views stacked vertically.
- **3D (top-bottom+flip)** expects stacked views where the eye order or orientation needs flipping.

This setting is separate from **Use right-eye input**. Use right-eye input when each eye is stored as a separate sequence; use the stereo layout setting when both eyes are packed into each image.

## Rotation and plane controls

Dome and sphere modes can rotate the layer with pitch, yaw, and roll. Plane mode exposes azimuth, elevation, roll, distance, horizontal offset, vertical offset, width, height, and aspect-fit controls.

## Region of interest

Enable **Region of interest** to render only a normalized rectangle from the source image. The image sequence preview can help choose the region interactively.