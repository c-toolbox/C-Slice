/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SPLITMEDIALOADER_H
#define CSLICE_SPLITMEDIALOADER_H

#include "slicetypes.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace CSlice {

struct SliceSourceOptions {
    std::filesystem::path input;
    std::string identifier;
    int start = 0;
    int stop = 0;
    int step = 1;
    int imageDelayMs = 0;
    std::string videoDecodingMode; // "Software", "Hardware", or "Hybrid"
    bool validateSequenceFrames = true;
};

struct SliceVerificationError {
    std::filesystem::path path;
    std::string message;
};

struct SliceVerificationResult {
    bool ok = false;
    int verifiedFrames = 0;
    int totalFrames = 0;
    int width = 0;
    int height = 0;
    SliceVerificationError error;
    std::vector<SliceVerificationError> errors;
    std::vector<SliceVerificationError> warnings;
};

using SliceVerificationProgressCallback = void (*)(int verifiedFrames, int totalFrames, int width, int height, void *userData);

struct SliceRenderOptions {
    GridMode gridMode = GridMode::Dome;
    StereoMode stereoMode = StereoMode::None;
    float alpha = 1.f;
    bool flipY = false;
    glm::vec3 rotate = glm::vec3(0.f);
    glm::vec3 translate = glm::vec3(0.f);
    bool roiEnabled = false;
    glm::vec4 roi = glm::vec4(0.f, 0.f, 1.f, 1.f);
    double planeAzimuth = 0.0;
    double planeElevation = 0.0;
    double planeRoll = 0.0;
    double planeDistance = 740.0;
    double planeHorizontal = 0.0;
    double planeVertical = 0.0;
    double planeWidth = 0.0;
    double planeHeight = 0.0;
    uint8_t planeAspectRatio = 1;
};

class SliceMediaLoader {
public:
    virtual ~SliceMediaLoader() = default;

    virtual void configureBudgets(int loadingThreadCount) = 0;
    virtual bool addSource(const SliceSourceOptions &sourceOptions,
        const SliceRenderOptions &renderOptions,
        std::string *errorMessage = nullptr) = 0;

    virtual void startLoading() = 0;
    virtual void setPaused(bool paused) = 0;
    virtual void resetLoadingFromFrame(int frameIndex) = 0;
    virtual void initializeGL() = 0;
    virtual void update(bool updateRendering = true) = 0;
    virtual void uploadPendingTextures(int frameIndex, bool forceCurrentFrame = false) = 0;
    virtual void advanceFrames() = 0;
    virtual bool showFrame(int frameIndex, bool allowFailedFrames = false) = 0;
    virtual void releaseFrame(int frameIndex) = 0;
    virtual void cleanup() = 0;

    virtual bool empty() const = 0;
    virtual bool ready() const = 0;
    virtual int layerCount() const = 0;
    virtual const SliceLayer* layer(int index) const = 0;
    virtual const SliceLayer* layerForEye(bool rightEye) const = 0;
    virtual int currentFrameIndex(bool rightEye = false) const = 0;
    virtual int loadedFrameCount() const = 0;
    virtual int totalFrameCount() const = 0;
    virtual int effectiveLoadingThreadCount() const = 0;
    virtual std::optional<SliceVerificationError> takeFirstFailure() = 0;
};

} // namespace CSlice

#endif // CSLICE_SLICEMEDIALOADER_H
