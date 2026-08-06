/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sunden
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SLICETYPES_H
#define CSLICE_SLICETYPES_H

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace CSlice {

enum class GridMode : std::uint8_t {
    None = 0,
    Plane = 1,
    Dome = 2,
    SphereEqr = 3,
    SphereEac = 4
};

enum class StereoMode : std::uint8_t {
    None = 0,
    SideBySide = 1,
    TopBottom = 2,
    TopBottomFlip = 3
};

struct SliceLayer {
    std::vector<unsigned char> pixels;
    unsigned int textureId = 0;
    int width = 0;
    int height = 0;
    GridMode gridMode = GridMode::Dome;
    StereoMode stereoMode = StereoMode::None;
    float alpha = 1.f;
    bool flipY = false;
    bool roiEnabled = false;
    glm::vec4 roi = glm::vec4(0.f, 0.f, 1.f, 1.f);
    glm::vec3 rotate = glm::vec3(0.f);
    glm::vec3 translate = glm::vec3(0.f);
    double planeAzimuth = 0.0;
    double planeElevation = 0.0;
    double planeRoll = 0.0;
    double planeDistance = 740.0;
    double planeHorizontal = 0.0;
    double planeVertical = 0.0;
    double planeWidth = 0.0;
    double planeHeight = 0.0;
    std::uint8_t planeAspectRatio = 1;
    bool textureReady = false;
};

} // namespace CSlice

#endif // CSLICE_SLICETYPES_H