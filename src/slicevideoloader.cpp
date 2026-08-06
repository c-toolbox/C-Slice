/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "slicevideoloader.h"
#include "slicesettings.h"
#include "ffmpegprobe.h"
#include <sgct/opengl.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <format>

namespace {

std::string mpvErrorString(int error)
{
    return mpv_error_string(error);
}

bool command(mpv_handle *handle, std::initializer_list<const char*> arguments, std::string *errorMessage = nullptr)
{
    std::vector<const char*> values(arguments);
    values.push_back(nullptr);
    const int result = mpv_command(handle, values.data());
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = mpvErrorString(result);
        }
        return false;
    }
    return true;
}

bool setOptionString(mpv_handle *handle, const char *name, const char *value, std::string *errorMessage = nullptr)
{
    const int result = mpv_set_option_string(handle, name, value);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = std::format("Failed to set mpv option {}: {}", name, mpvErrorString(result));
        }
        return false;
    }
    return true;
}

bool getPropertyDouble(mpv_handle *handle, const char *name, double &value)
{
    double property = 0.0;
    const int result = mpv_get_property(handle, name, MPV_FORMAT_DOUBLE, &property);
    if (result < 0 || !std::isfinite(property)) {
        return false;
    }
    value = property;
    return true;
}

bool getPropertyInt64(mpv_handle *handle, const char *name, std::int64_t &value)
{
    std::int64_t property = 0;
    const int result = mpv_get_property(handle, name, MPV_FORMAT_INT64, &property);
    if (result < 0) {
        return false;
    }
    value = property;
    return true;
}

void waitForFileLoaded(mpv_handle *handle)
{
    while (true) {
        mpv_event *event = mpv_wait_event(handle, 5.0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            return;
        }
        if (event->event_id == MPV_EVENT_FILE_LOADED || event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_SHUTDOWN) {
            return;
        }
    }
}

std::string pathToUtf8(const std::filesystem::path &path)
{
#ifdef _WIN32
    const auto text = std::filesystem::path(path).u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
#else
    return path.string();
#endif
}

} // namespace

namespace CSlice {

SliceVideoLoader::~SliceVideoLoader()
{
    cleanup();
}

void SliceVideoLoader::configureBudgets(int)
{
}

bool SliceVideoLoader::addSource(const SliceSourceOptions &sourceOptions,
    const SliceRenderOptions &renderOptions,
    std::string *errorMessage)
{
    if (m_sources.empty()) {
        m_sources.reserve(2);
    }

    SourceState source;
    source.input = sourceOptions.input;
    source.identifier = sourceOptions.identifier;
    source.renderOptions = renderOptions;
    source.metadata = probeMetadata(source.input);
    if (!source.metadata.ok) {
        if (errorMessage) {
            *errorMessage = source.metadata.error;
        }
        return false;
    }

    source.start = std::clamp(sourceOptions.start, 0, std::max(0, source.metadata.frameCount - 1));
    source.stop = std::clamp(sourceOptions.stop, 0, std::max(0, source.metadata.frameCount - 1));
    source.step = std::max(1, sourceOptions.step);
    source.direction = source.stop >= source.start ? 1 : -1;
    source.currentFrameIndex = source.start;
    source.totalFrameCount = std::max(1, (std::abs(source.stop - source.start) / source.step) + 1);

    source.currentLayer.width = source.metadata.width;
    source.currentLayer.height = source.metadata.height;
    source.currentLayer.textureReady = false;
    applyRenderOptions(source.currentLayer, renderOptions);

    // Determine if this is a right-eye source (second source added)
    const bool isRightEye = m_sources.size() == 1;

    // Apply video decoding mode from source options
    source.video = std::make_unique<MpvVideo>();
    MpvVideo::DecodingMode decodingMode = MpvVideo::DecodingMode::Software;
    if (sourceOptions.videoDecodingMode == "Hardware") {
        decodingMode = MpvVideo::DecodingMode::Hardware;
    } else if (sourceOptions.videoDecodingMode == "Hybrid") {
        decodingMode = MpvVideo::DecodingMode::Hybrid;
    }
    source.video->setDecodingMode(decodingMode);

    // For Hybrid mode: left eye uses hardware, right eye uses software
    if (decodingMode == MpvVideo::DecodingMode::Hybrid) {
        source.video->setHybridRightEye(isRightEye);
    }

    if (!source.video->initializeMpv(errorMessage)) {
        destroySource(source);
        return false;
    }
    if (!source.video->load(source.input, errorMessage)) {
        destroySource(source);
        return false;
    }
    source.video->setFlipY(source.currentLayer.flipY);
    {
        const double timestamp = timestampForFrame(source, source.currentFrameIndex);
        source.video->setTimePosition(timestamp);
    }

    m_sources.push_back(std::move(source));
    if (m_sources.size() == 1) {
        m_currentFrameIndex = m_sources.front().start;
    }
    return true;
}

void SliceVideoLoader::startLoading()
{
    m_started = true;
}

void SliceVideoLoader::setPaused(bool paused)
{
    m_paused = paused;
}

void SliceVideoLoader::resetLoadingFromFrame(int frameIndex)
{
    m_currentFrameIndex = frameIndex;
    for (SourceState &source : m_sources) {
        source.currentFrameIndex = clampFrameIndex(source, frameIndex);
        source.needsRender = true;
        source.loaded = false;
        double timestamp = timestampForFrame(source, source.currentFrameIndex);
        if (source.video) {
            source.video->setTimePosition(timestamp);
        }
    }
}

void SliceVideoLoader::initializeGL()
{
    for (SourceState &source : m_sources) {
        if (source.initialized || !source.video) {
            continue;
        }

        std::string error;
        if (!source.video->initializeGL(&error)) {
            m_failures.push_back({ source.input, error });
            destroySource(source);
            continue;
        }

        // Start the mpv event loop thread for this source.
        // This allows mpv to process events and decode frames in the background,
        // overlapping decoding with other slicing operations.
        source.video->startEventLoop();

        source.currentLayer.textureId = source.video->textureId();
        source.currentLayer.textureReady = false;
        source.initialized = true;
        source.needsRender = true;
    }
}

void SliceVideoLoader::update(bool updateRendering)
{
    if (!updateRendering) {
        return;
    }
    uploadPendingTextures(m_currentFrameIndex, false);
}

void SliceVideoLoader::uploadPendingTextures(int frameIndex, bool)
{
    if (!m_started || m_paused) {
        return;
    }
    // Issue seek/step commands to all sources before rendering any of them so
    // that the hardware video decoder can work on all streams in parallel while
    // we block on each render call in turn.
    for (SourceState &source : m_sources) {
        if (!source.video) {
            continue;
        }
        const int clampedFrame = clampFrameIndex(source, frameIndex);
        if (source.currentFrameIndex != clampedFrame) {
            const bool nextFrame = clampedFrame == source.currentFrameIndex + (source.step * source.direction);
            source.currentFrameIndex = clampedFrame;
            source.needsRender = true;
            source.loaded = false;
            if (nextFrame) {
                source.video->frameStep();
            }
            else {
                double timestamp = timestampForFrame(source, source.currentFrameIndex);
                source.video->setTimePosition(timestamp);
            }
        }
    }
    for (SourceState &source : m_sources) {
        if (!source.video) {
            continue;
        }
        if (source.needsRender) {
            renderSource(source);
        }
    }
}

void SliceVideoLoader::advanceFrames()
{
    for (SourceState &source : m_sources) {
        if (source.video) {
            source.video->frameStep();
            source.needsRender = true;
            source.loaded = false;
        }
    }
}

bool SliceVideoLoader::showFrame(int frameIndex, bool allowFailedFrames)
{
    uploadPendingTextures(frameIndex, true);
    bool allReady = !m_sources.empty();
    int loaded = 0;
    for (SourceState &source : m_sources) {
        allReady = allReady && source.currentLayer.textureReady;
        if (source.currentLayer.textureReady) {
            ++loaded;
        }
    }
    if (allReady) {
        m_currentFrameIndex = frameIndex;
    }
    // When the caller says failed/missing frames are acceptable, treat partially-loaded
    // output as success so slicing is not stalled by a problematic right-eye source.
    if (allowFailedFrames && loaded > 0) {
        m_currentFrameIndex = frameIndex;
        return true;
    }
    return allReady || loaded > 0;
}

void SliceVideoLoader::releaseFrame(int)
{
}

void SliceVideoLoader::cleanup()
{
    for (SourceState &source : m_sources) {
        destroySource(source);
    }
    m_sources.clear();
}

bool SliceVideoLoader::empty() const
{
    return m_sources.empty();
}

bool SliceVideoLoader::ready() const
{
    return !m_sources.empty();
}

int SliceVideoLoader::layerCount() const
{
    return static_cast<int>(m_sources.size());
}

const SliceLayer* SliceVideoLoader::layer(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_sources.size())) {
        return nullptr;
    }
    return &m_sources[static_cast<std::size_t>(index)].currentLayer;
}

const SliceLayer* SliceVideoLoader::layerForEye(bool rightEye) const
{
    if (m_sources.empty()) {
        return nullptr;
    }
    if (rightEye && m_sources.size() > 1) {
        return &m_sources[1].currentLayer;
    }
    return &m_sources[0].currentLayer;
}

int SliceVideoLoader::currentFrameIndex(bool) const
{
    return m_currentFrameIndex;
}

int SliceVideoLoader::loadedFrameCount() const
{
    if (m_sources.empty()) {
        return 0;
    }
    int loaded = m_sources.front().loadedFrameCount;
    for (const SourceState &source : m_sources) {
        loaded = std::min(loaded, source.loadedFrameCount);
    }
    return loaded;
}

int SliceVideoLoader::totalFrameCount() const
{
    if (m_sources.empty()) {
        return 0;
    }
    int total = m_sources.front().totalFrameCount;
    for (const SourceState &source : m_sources) {
        total = std::min(total, source.totalFrameCount);
    }
    return total;
}

int SliceVideoLoader::effectiveLoadingThreadCount() const
{
    return 1;
}

std::optional<SliceVerificationError> SliceVideoLoader::takeFirstFailure()
{
    if (m_failures.empty()) {
        return std::nullopt;
    }
    SliceVerificationError failure = m_failures.front();
    m_failures.pop_front();
    return failure;
}

SliceVideoLoader::Metadata SliceVideoLoader::probeMetadata(const std::filesystem::path &path)
{
    Metadata metadata;
    mpv_handle *handle = mpv_create();
    if (!handle) {
        metadata.error = "Failed to create mpv handle.";
        return metadata;
    }

    std::string error;
    setOptionString(handle, "vo", "null", &error);
    setOptionString(handle, "ao", "null", &error);
    setOptionString(handle, "pause", "yes", &error);
    setOptionString(handle, "idle", "yes", &error);

    int result = mpv_initialize(handle);
    if (result < 0) {
        metadata.error = std::format("Failed to initialize mpv: {}", mpvErrorString(result));
        mpv_destroy(handle);
        return metadata;
    }

    const std::string inputPath = pathToUtf8(path);
    if (!command(handle, { "loadfile", inputPath.c_str() }, &error)) {
        metadata.error = std::format("Failed to load video {}: {}", inputPath, error);
        mpv_terminate_destroy(handle);
        return metadata;
    }
    waitForFileLoaded(handle);

    std::int64_t width = 0;
    std::int64_t height = 0;
    double duration = 0.0;
    double fps = 0.0;
    double estimatedFrames = 0.0;
    getPropertyInt64(handle, "width", width);
    getPropertyInt64(handle, "height", height);
    getPropertyDouble(handle, "duration", duration);
    getPropertyDouble(handle, "container-fps", fps);
    if (fps <= 0.0) {
        getPropertyDouble(handle, "estimated-vf-fps", fps);
    }
    getPropertyDouble(handle, "estimated-frame-count", estimatedFrames);

    metadata.width = static_cast<int>(std::max<std::int64_t>(0, width));
    metadata.height = static_cast<int>(std::max<std::int64_t>(0, height));
    metadata.durationSeconds = std::max(0.0, duration);
    metadata.fps = fps > 0.0 ? fps : 1.0;
    const int accurateFrames = FFmpegProbe::accurateFrameCount(path);
    metadata.frameCount = accurateFrames > 0
        ? accurateFrames
        : (estimatedFrames > 0.0
            ? static_cast<int>(std::ceil(estimatedFrames))
            : static_cast<int>(std::ceil(metadata.durationSeconds * metadata.fps)));
    metadata.frameCount = std::max(1, metadata.frameCount);
    metadata.ok = metadata.width > 0 && metadata.height > 0;
    if (!metadata.ok) {
        metadata.error = std::format("Failed to read video metadata for {}.", inputPath);
    }

    mpv_terminate_destroy(handle);
    return metadata;
}

void SliceVideoLoader::applyRenderOptions(SliceLayer &layer, const SliceRenderOptions &options)
{
    layer.gridMode = options.gridMode;
    layer.stereoMode = options.stereoMode;
    layer.alpha = options.alpha;
    layer.flipY = options.flipY;
    layer.rotate = options.rotate;
    layer.translate = options.translate;
    layer.roiEnabled = options.roiEnabled;
    layer.roi = options.roi;
    layer.planeAzimuth = options.planeAzimuth;
    layer.planeElevation = options.planeElevation;
    layer.planeRoll = options.planeRoll;
    layer.planeDistance = options.planeDistance;
    layer.planeHorizontal = options.planeHorizontal;
    layer.planeVertical = options.planeVertical;
    layer.planeWidth = options.planeWidth;
    layer.planeHeight = options.planeHeight;
    layer.planeAspectRatio = options.planeAspectRatio;
}

void SliceVideoLoader::destroySource(SourceState &source)
{
    source.video.reset();
    source.currentLayer.textureId = 0;
    source.currentLayer.textureReady = false;
}

void SliceVideoLoader::renderSource(SourceState &source)
{
    if (!source.video) {
        return;
    }

    source.video->render();

    source.currentLayer.textureId = source.video->textureId();
    source.currentLayer.textureReady = true;
    source.loaded = true;
    source.needsRender = false;
    source.loadedFrameCount = std::max(source.loadedFrameCount, sequenceOffsetForFrame(source, source.currentFrameIndex) + 1);

    // After rendering the current frame (and after the mpv texture has been
    // rendered), queue a frame-step on the event loop thread so that decoding
    // of the next frame happens in parallel with other operations (slicing,
    // encoding, OpenGL readback). If other operations have already triggered
    // this decode via the render update callback, advanceNextFrameAsync is
    // still safe to call -- it simply queues another frame-step that mpv will
    // execute when the current frame is consumed.
    source.video->advanceNextFrameAsync();
}

int SliceVideoLoader::clampFrameIndex(const SourceState &source, int frameIndex) const
{
    if (source.direction > 0) {
        return std::clamp(frameIndex, source.start, source.stop);
    }
    return std::clamp(frameIndex, source.stop, source.start);
}

int SliceVideoLoader::sequenceOffsetForFrame(const SourceState &source, int frameIndex) const
{
    const int distance = std::abs(clampFrameIndex(source, frameIndex) - source.start);
    return std::clamp(distance / std::max(1, source.step), 0, std::max(0, source.totalFrameCount - 1));
}

double SliceVideoLoader::timestampForFrame(const SourceState &source, int frameIndex) const
{
    const int clampedFrame = std::clamp(frameIndex, 0, std::max(0, source.metadata.frameCount - 1));
    return clampedFrame / std::max(0.001, source.metadata.fps);
}

} // namespace CSlice
