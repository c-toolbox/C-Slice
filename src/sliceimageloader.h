/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SPLITIMAGELOADER_H
#define CSLICE_SPLITIMAGELOADER_H

#include "slicemedialoader.h"

#include "slicewuffs.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <QString>

namespace CSlice {

class SliceImageLoader : public SliceMediaLoader {
public:
    using SourceOptions = SliceSourceOptions;
    using VerificationError = SliceVerificationError;
    using VerificationResult = SliceVerificationResult;
    using VerificationProgressCallback = SliceVerificationProgressCallback;
    using RenderOptions = SliceRenderOptions;

    SliceImageLoader() = default;
    SliceImageLoader(const SliceImageLoader&) = delete;
    SliceImageLoader& operator=(const SliceImageLoader&) = delete;
    ~SliceImageLoader() override;

    void configureBudgets(int loadingThreadCount) override;

    bool addSource(const SourceOptions &sourceOptions,
                   const RenderOptions &renderOptions,
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
    std::optional<VerificationError> takeFirstFailure() override;

    static VerificationResult verifySource(const SourceOptions &sourceOptions,
        VerificationProgressCallback progressCallback = nullptr,
        void *progressUserData = nullptr,
        int sizeWarningPercent = 0);

private:
    struct FrameSlot {
        int frameIndex = 0;
        std::size_t sequenceIndex = 0;
        std::filesystem::path path;
        unsigned int textureId = 0;
        bool queued = false;
        bool decoding = false;
        bool decoded = false;
        bool uploaded = false;
        bool loadedOnce = false;
        bool failed = false;
        std::string error;
    };

    struct DecodeBuffer {
        enum class State {
            Empty,
            Decoding,
            Decoded,
            Uploading,
            Uploaded,
            Failed
        };

        Wuffs::Image image;
        std::filesystem::path path;
        std::size_t sourceIndex = 0;
        std::size_t sequenceIndex = 0;
        int frameIndex = 0;
        State state = State::Empty;
        std::string error;
    };

    enum class LoaderSlotState {
        Idle,
        DecodeQueued,
        Decoding,
        Decoded,
        Failed,
        Stopping
    };

    struct LoaderSlot {
        mutable std::mutex mutex;
        std::condition_variable workAvailable;
        std::thread worker;
        Wuffs::DecodeContext decodeContext;
        DecodeBuffer buffers[2];
        unsigned int pbo = 0;
        unsigned int textureId = 0;
        std::size_t pboBytes = 0;
        int width = 0;
        int height = 0;
        int textureWidth = 0;
        int textureHeight = 0;
        Wuffs::Image::PixelDataFormat format = Wuffs::Image::PixelDataFormat::Rgb8;
        Wuffs::Image::PixelDataFormat textureFormat = Wuffs::Image::PixelDataFormat::Rgb8;
        std::size_t textureSourceIndex = std::numeric_limits<std::size_t>::max();
        std::size_t textureSequenceIndex = std::numeric_limits<std::size_t>::max();
        int textureFrameIndex = 0;
        bool textureAssigned = false;
        int decodeBufferIndex = 0;
        int uploadBufferIndex = -1;
        LoaderSlotState state = LoaderSlotState::Idle;
        bool stopping = false;
    };

    struct SourceState {
        std::string identifier;
        RenderOptions renderOptions;
        SliceLayer currentLayer;
        std::deque<FrameSlot> frames;
        std::filesystem::path input;
        QString directory;
        QString prefix;
        QString suffix;
        int start = 0;
        int stop = 0;
        int step = 1;
        int direction = 1;
        int digitCount = 0;
        int totalFrameCount = 0;
        int loadedFrameCount = 0;
        bool sequence = false;
        std::map<int, std::filesystem::path> resolvedFramePaths;
    };

    static void applyRenderOptions(SliceLayer &layer, const RenderOptions &options);
    static std::deque<FrameSlot> buildFrameList(const SourceOptions &sourceOptions, std::string *errorMessage);
    static bool configureSourceState(SourceState &source, const SourceOptions &sourceOptions, std::string *errorMessage);
    static int frameIndexAt(const SourceState &source, std::size_t sequenceIndex);
    static FrameSlot buildFrameSlot(const SourceState &source, std::size_t sequenceIndex);
    static FrameSlot* ensureFrameForSequenceIndex(SourceState &source, std::size_t sequenceIndex);
    static std::optional<std::size_t> sequenceIndexForFrame(const SourceState &source, int frameIndex);

    void startWorkers();
    void stopWorkers();
    void scheduleFrames();
    void scheduleFramesLocked();
    void workerLoop(std::size_t workerIndex);
    void ensureGlResources();
    void uploadDecodedSlots(int frameIndex, bool forceCurrentFrame);
    void uploadBufferToTexture(LoaderSlot &slot, DecodeBuffer &buffer);
    void clearFrameStateLocked(FrameSlot &frame);
    const LoaderSlot* textureOwnerLocked(std::size_t sourceIndex, std::size_t sequenceIndex, unsigned int textureId) const;
    bool frameTextureIsCurrentLocked(std::size_t sourceIndex, const FrameSlot &frame) const;
    bool canOverwriteSlotTextureLocked(const LoaderSlot &slot) const;
    void releaseFramesUpToSequenceIndexLocked(std::size_t sequenceIndex);
    FrameSlot* pendingFrameForSequenceIndex(SourceState &source, std::size_t sourceIndex, std::size_t sequenceIndex) const;
    FrameSlot* frameForSequenceIndex(SourceState &source, std::size_t sequenceIndex);
    const FrameSlot* frameForSequenceIndex(const SourceState &source, std::size_t sequenceIndex) const;
    FrameSlot* frameForIndex(SourceState &source, int frameIndex);
    const FrameSlot* frameForIndex(const SourceState &source, int frameIndex) const;
    const SliceLayer* currentLayerForEye(bool rightEye) const;
    std::optional<std::size_t> sequenceIndexForFrameLocked(int frameIndex) const;
    bool frameHasPendingSlotLocked(std::size_t sourceIndex, std::size_t sequenceIndex) const;
    std::optional<std::pair<std::size_t, std::size_t>> nextFrameToScheduleLocked(std::size_t slotIndex);

    std::vector<SourceState> m_sources;
    std::vector<std::unique_ptr<LoaderSlot>> m_loaderSlots;
    mutable std::mutex m_mutex;
    std::condition_variable m_frameAvailable;
    std::deque<VerificationError> m_failures;
    bool m_stopping = false;
    bool m_paused = false;
    int m_loadingThreadCount = 1;
    int m_effectiveLoadingThreadCount = 1;
    int m_currentFrameIndex = 0;
    std::size_t m_currentFrameSequenceIndex = 0;
    std::optional<std::size_t> m_releasedFrameSequenceIndex;
    std::size_t m_nextScheduleSequenceIndex = 0;
};

} // namespace CSlice

#endif // CSLICE_SLICEIMAGELOADER_H
