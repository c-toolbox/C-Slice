/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SPLITVIDEOLOADER_H
#define CSLICE_SPLITVIDEOLOADER_H

#include "slicemedialoader.h"
#include "mpvvideo.h"

#include <deque>
#include <memory>
#include <mutex>

namespace CSlice {

class SliceVideoLoader : public SliceMediaLoader {
public:
    SliceVideoLoader() = default;
    SliceVideoLoader(const SliceVideoLoader&) = delete;
    SliceVideoLoader& operator=(const SliceVideoLoader&) = delete;
    ~SliceVideoLoader() override;

    void configureBudgets(int loadingThreadCount) override;
    bool addSource(const SliceSourceOptions &sourceOptions,
        const SliceRenderOptions &renderOptions,
        std::string *errorMessage = nullptr) override;

    void startLoading() override;
    void setPaused(bool paused) override;
    void resetLoadingFromFrame(int frameIndex) override;
    void initializeGL() override;
    void update(bool updateRendering = true) override;
    void uploadPendingTextures(int frameIndex, bool forceCurrentFrame = false) override;
    void advanceFrames() override;
    bool showFrame(int frameIndex, bool allowFailedFrames = false) override;
    void releaseFrame(int frameIndex) override;
    void cleanup() override;

    bool empty() const override;
    bool ready() const override;
    int layerCount() const override;
    const SliceLayer* layer(int index) const override;
    const SliceLayer* layerForEye(bool rightEye) const override;
    int currentFrameIndex(bool rightEye = false) const override;
    int loadedFrameCount() const override;
    int totalFrameCount() const override;
    int effectiveLoadingThreadCount() const override;
    std::optional<SliceVerificationError> takeFirstFailure() override;

    struct Metadata {
        bool ok = false;
        double durationSeconds = 0.0;
        double fps = 0.0;
        int width = 0;
        int height = 0;
        int frameCount = 0;
        std::string error;
    };

    static Metadata probeMetadata(const std::filesystem::path &path);

private:
    struct SourceState {
        std::filesystem::path input;
        std::string identifier;
        SliceRenderOptions renderOptions;
        SliceLayer currentLayer;
        Metadata metadata;
        std::unique_ptr<MpvVideo> video;
        int start = 0;
        int stop = 0;
        int step = 1;
        int direction = 1;
        int currentFrameIndex = 0;
        int loadedFrameCount = 0;
        int totalFrameCount = 0;
        bool initialized = false;
        bool loaded = false;
        bool needsRender = true;
    };

    static void applyRenderOptions(SliceLayer &layer, const SliceRenderOptions &options);

    void destroySource(SourceState &source);
    void renderSource(SourceState &source);
    int clampFrameIndex(const SourceState &source, int frameIndex) const;
    int sequenceOffsetForFrame(const SourceState &source, int frameIndex) const;
    double timestampForFrame(const SourceState &source, int frameIndex) const;

    std::vector<SourceState> m_sources;
    mutable std::mutex m_mutex;
    std::deque<SliceVerificationError> m_failures;
    bool m_started = false;
    bool m_paused = false;
    int m_currentFrameIndex = 0;
};

} // namespace CSlice

#endif // CSLICE_SLICEVIDEOLOADER_H
