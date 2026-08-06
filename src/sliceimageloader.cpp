/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sliceimageloader.h"

#include "utils/imagesequenceutils.h"

#include <sgct/opengl.h>

#include <QString>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <iostream>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

struct VerificationFileSize {
    std::uintmax_t bytes = 0;
    bool known = false;
};

QString pathToQString(const std::filesystem::path &path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::filesystem::path pathFromQString(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

struct GlPixelFormat {
    unsigned int internalFormat = GL_RGB8;
    unsigned int format = GL_RGB;
};

GlPixelFormat glPixelFormat(CSlice::Wuffs::Image::PixelDataFormat format)
{
    switch (format) {
        case CSlice::Wuffs::Image::PixelDataFormat::Gray8:
            return { GL_R8, GL_RED };
        case CSlice::Wuffs::Image::PixelDataFormat::Rgb8:
            return { GL_RGB8, GL_RGB };
        case CSlice::Wuffs::Image::PixelDataFormat::Bgr8:
            return { GL_RGB8, GL_BGR };
        case CSlice::Wuffs::Image::PixelDataFormat::Rgba8:
            return { GL_RGBA8, GL_RGBA };
        case CSlice::Wuffs::Image::PixelDataFormat::Bgra8:
            return { GL_RGBA8, GL_BGRA };
    }
    return { GL_RGB8, GL_RGB };
}

std::string formatFileSize(std::uintmax_t bytes)
{
    constexpr std::uintmax_t KiB = 1024;
    constexpr std::uintmax_t MiB = KiB * 1024;
    if (bytes >= MiB) {
        return std::format("{:.1f} MiB", static_cast<double>(bytes) / static_cast<double>(MiB));
    }
    if (bytes >= KiB) {
        return std::format("{:.1f} KiB", static_cast<double>(bytes) / static_cast<double>(KiB));
    }
    return std::format("{} bytes", bytes);
}

bool isSmallerByPercent(std::uintmax_t bytes, std::uintmax_t referenceBytes, int percent)
{
    if (percent <= 0 || referenceBytes == 0 || bytes >= referenceBytes) {
        return false;
    }
    const double ratio = static_cast<double>(bytes) / static_cast<double>(referenceBytes);
    return ratio <= (1.0 - static_cast<double>(percent) / 100.0);
}

} // namespace

namespace CSlice {

SliceImageLoader::~SliceImageLoader()
{
    cleanup();
}

void SliceImageLoader::configureBudgets(int loadingThreadCount)
{
    m_loadingThreadCount = std::clamp(loadingThreadCount, 1, 64);
    m_effectiveLoadingThreadCount = m_loadingThreadCount;
}

bool SliceImageLoader::addSource(const SourceOptions &sourceOptions,
                            const RenderOptions &renderOptions,
                            std::string *errorMessage)
{
    if (sourceOptions.input.empty()) {
        if (errorMessage) {
            *errorMessage = "Layer input path is empty";
        }
        return false;
    }

    SourceState source;
    if (!configureSourceState(source, sourceOptions, errorMessage)) {
        return false;
    }

    source.identifier = sourceOptions.identifier;
    source.renderOptions = renderOptions;
    applyRenderOptions(source.currentLayer, renderOptions);

    {
        std::lock_guard lock(m_mutex);
        source.frames = buildFrameList(sourceOptions, errorMessage);
        if (source.frames.empty()) {
            if (errorMessage && errorMessage->empty()) {
                *errorMessage = "No input frames were found";
            }
            return false;
        }
        m_sources.push_back(std::move(source));
    }

    return true;
}

void SliceImageLoader::startLoading()
{
    startWorkers();
    scheduleFrames();
}

void SliceImageLoader::setPaused(bool paused)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_paused == paused) {
            return;
        }
        m_paused = paused;
        if (!m_paused) {
            m_failures.clear();
        }
    }
    if (!paused) {
        scheduleFrames();
    }
}

void SliceImageLoader::resetLoadingFromFrame(int frameIndex)
{
    {
        std::lock_guard lock(m_mutex);
        m_currentFrameIndex = frameIndex;
        if (const std::optional<std::size_t> sequenceIndex = sequenceIndexForFrameLocked(frameIndex)) {
            m_currentFrameSequenceIndex = *sequenceIndex;
            m_nextScheduleSequenceIndex = *sequenceIndex;
            if (*sequenceIndex == 0) {
                m_releasedFrameSequenceIndex.reset();
            }
            else {
                m_releasedFrameSequenceIndex = *sequenceIndex - 1u;
            }
        }

        m_failures.clear();
        for (SourceState &source : m_sources) {
            source.loadedFrameCount = 0;
            for (FrameSlot &frame : source.frames) {
                if (m_releasedFrameSequenceIndex && frame.sequenceIndex <= *m_releasedFrameSequenceIndex && frame.loadedOnce) {
                    ++source.loadedFrameCount;
                    continue;
                }
                clearFrameStateLocked(frame);
            }
        }
    }

    for (const std::unique_ptr<LoaderSlot> &slot : m_loaderSlots) {
        std::lock_guard slotLock(slot->mutex);
        if (slot->state == LoaderSlotState::DecodeQueued || slot->state == LoaderSlotState::Decoded || slot->state == LoaderSlotState::Failed) {
            slot->state = LoaderSlotState::Idle;
            slot->uploadBufferIndex = -1;
            for (DecodeBuffer &buffer : slot->buffers) {
                buffer.state = DecodeBuffer::State::Empty;
                buffer.error.clear();
            }
        }
    }

    scheduleFrames();
}

void SliceImageLoader::initializeGL()
{
    ensureGlResources();
    scheduleFrames();
}

void SliceImageLoader::update(bool updateRendering)
{
    (void)updateRendering;
    scheduleFrames();
}

void SliceImageLoader::uploadPendingTextures(int frameIndex, bool forceCurrentFrame)
{
    ensureGlResources();
    {
        std::lock_guard lock(m_mutex);
        m_currentFrameIndex = frameIndex;
        if (const std::optional<std::size_t> sequenceIndex = sequenceIndexForFrameLocked(frameIndex)) {
            m_currentFrameSequenceIndex = *sequenceIndex;
            m_nextScheduleSequenceIndex = std::max(m_nextScheduleSequenceIndex, *sequenceIndex);
        }
    }

    uploadDecodedSlots(frameIndex, forceCurrentFrame);
    scheduleFrames();
}

void SliceImageLoader::advanceFrames()
{
    int nextFrame = 0;
    {
        std::lock_guard lock(m_mutex);
        nextFrame = m_currentFrameIndex + 1;
    }
    showFrame(nextFrame);
}

bool SliceImageLoader::showFrame(int frameIndex, bool allowFailedFrames)
{
    uploadPendingTextures(frameIndex, true);

    bool allReady = true;
    {
        std::lock_guard lock(m_mutex);
        m_currentFrameIndex = frameIndex;
        if (const std::optional<std::size_t> sequenceIndex = sequenceIndexForFrameLocked(frameIndex)) {
            m_currentFrameSequenceIndex = *sequenceIndex;
            m_nextScheduleSequenceIndex = std::max(m_nextScheduleSequenceIndex, *sequenceIndex + 1u);
        }

        for (std::size_t sourceIndex = 0; sourceIndex < m_sources.size(); ++sourceIndex) {
            SourceState &source = m_sources[sourceIndex];
            FrameSlot *frame = frameForIndex(source, frameIndex);
            if (!frame) {
                allReady = false;
                continue;
            }

            if (frame->failed && allowFailedFrames) {
                applyRenderOptions(source.currentLayer, source.renderOptions);
                continue;
            }

            const LoaderSlot *owner = textureOwnerLocked(sourceIndex, frame->sequenceIndex, frame->textureId);
            if (frame->failed || !frame->uploaded || frame->textureId == 0 || !owner) {
                frame->uploaded = false;
                frame->textureId = 0;
                allReady = false;
                continue;
            }

            applyRenderOptions(source.currentLayer, source.renderOptions);
            source.currentLayer.textureId = frame->textureId;
            source.currentLayer.width = owner->width;
            source.currentLayer.height = owner->height;
            source.currentLayer.textureReady = true;
        }
    }

    scheduleFrames();
    return allReady && !m_sources.empty();
}

void SliceImageLoader::releaseFrame(int frameIndex)
{
    {
        std::lock_guard lock(m_mutex);
        if (const std::optional<std::size_t> sequenceIndex = sequenceIndexForFrameLocked(frameIndex)) {
            releaseFramesUpToSequenceIndexLocked(*sequenceIndex);
        }
    }
    scheduleFrames();
}

void SliceImageLoader::cleanup()
{
    stopWorkers();

    for (const std::unique_ptr<LoaderSlot> &slot : m_loaderSlots) {
        if (slot->pbo != 0) {
            glDeleteBuffers(1, &slot->pbo);
            slot->pbo = 0;
        }
        if (slot->textureId != 0) {
            glDeleteTextures(1, &slot->textureId);
            slot->textureId = 0;
        }
    }

    std::lock_guard lock(m_mutex);
    m_loaderSlots.clear();
    m_sources.clear();
    m_failures.clear();
    m_currentFrameSequenceIndex = 0;
    m_releasedFrameSequenceIndex.reset();
    m_nextScheduleSequenceIndex = 0;
}

bool SliceImageLoader::empty() const
{
    std::lock_guard lock(m_mutex);
    return m_sources.empty();
}

bool SliceImageLoader::ready() const
{
    std::lock_guard lock(m_mutex);
    if (m_sources.empty()) {
        return false;
    }
    return std::ranges::all_of(m_sources, [](const SourceState& source) {
        return source.currentLayer.textureReady && source.currentLayer.textureId != 0;
    });
}

int SliceImageLoader::layerCount() const
{
    std::lock_guard lock(m_mutex);
    return static_cast<int>(m_sources.size());
}

const SliceLayer* SliceImageLoader::layer(int index) const
{
    std::lock_guard lock(m_mutex);
    if (index < 0 || index >= static_cast<int>(m_sources.size())) {
        return nullptr;
    }
    return &m_sources[static_cast<std::size_t>(index)].currentLayer;
}

const SliceLayer* SliceImageLoader::layerForEye(bool rightEye) const
{
    return currentLayerForEye(rightEye);
}

int SliceImageLoader::currentFrameIndex(bool rightEye) const
{
    (void)rightEye;
    std::lock_guard lock(m_mutex);
    return m_currentFrameIndex;
}

int SliceImageLoader::loadedFrameCount() const
{
    std::lock_guard lock(m_mutex);
    if (m_sources.empty()) {
        return 0;
    }
    return std::ranges::min(m_sources, {}, &SourceState::loadedFrameCount).loadedFrameCount;
}

int SliceImageLoader::totalFrameCount() const
{
    std::lock_guard lock(m_mutex);
    return m_sources.empty() ? 0 : m_sources.front().totalFrameCount;
}

int SliceImageLoader::effectiveLoadingThreadCount() const
{
    std::lock_guard lock(m_mutex);
    return m_effectiveLoadingThreadCount;
}

std::optional<SliceImageLoader::VerificationError> SliceImageLoader::takeFirstFailure()
{
    std::lock_guard lock(m_mutex);
    if (m_failures.empty()) {
        return std::nullopt;
    }

    VerificationError failure = std::move(m_failures.front());
    m_failures.pop_front();
    return failure;
}
void SliceImageLoader::applyRenderOptions(SliceLayer &layer, const RenderOptions &options)
{
    layer.gridMode = options.gridMode;
    layer.stereoMode = options.stereoMode;
    layer.alpha = std::clamp(options.alpha, 0.f, 1.f);
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

std::deque<SliceImageLoader::FrameSlot> SliceImageLoader::buildFrameList(const SourceOptions &sourceOptions, std::string *errorMessage)
{
    SourceState source;
    if (!configureSourceState(source, sourceOptions, errorMessage)) {
        return {};
    }

    std::deque<FrameSlot> frames;
    if (!sourceOptions.validateSequenceFrames) {
        frames.push_back(buildFrameSlot(source, 0));
        return frames;
    }

    for (int index = 0; index < source.totalFrameCount; ++index) {
        frames.push_back(buildFrameSlot(source, static_cast<std::size_t>(index)));
    }

    return frames;
}

bool SliceImageLoader::configureSourceState(SourceState &source, const SourceOptions &sourceOptions, std::string *errorMessage)
{
    const int start = sourceOptions.start;
    const int stop = sourceOptions.stop;
    const int step = std::max(1, sourceOptions.step);
    const QString inputPath = pathToQString(sourceOptions.input);
    const ImageSequenceScanResult sequenceInfo = ImageSequenceUtils::parseImageSequencePattern(inputPath, false);
    const QString stem = QString::fromStdString(sourceOptions.input.stem().string());
    const int digitCount = sequenceInfo.ok
        ? sequenceInfo.digitCount
        : ImageSequenceUtils::trailingDigitCount(stem);
    const bool sequence = start != stop;

    if (sequence && digitCount <= 0) {
        if (errorMessage) {
            *errorMessage = std::format("Input '{}' does not end with numbered frame digits", sourceOptions.input.string());
        }
        return false;
    }

    source.input = sourceOptions.input;
    source.start = start;
    source.stop = stop;
    source.step = step;
    source.direction = stop >= start ? 1 : -1;
    source.sequence = sequence;
    source.digitCount = digitCount;
    source.totalFrameCount = ImageSequenceUtils::expectedFrameCount(start, stop, step);

    if (!sequence) {
        source.totalFrameCount = 1;
        return true;
    }

    source.directory = pathToQString(sourceOptions.input.parent_path());
    source.prefix = sequenceInfo.ok
        ? sequenceInfo.prefix
        : stem.left(stem.size() - digitCount);
    source.suffix = sequenceInfo.ok
        ? sequenceInfo.suffix
        : QString::fromStdString(sourceOptions.input.extension().string()).remove(0, sourceOptions.input.extension().empty() ? 0 : 1);

    if (!sourceOptions.validateSequenceFrames) {
        return true;
    }

    const std::vector<ImageSequenceFrameInfo> resolvedFrames = ImageSequenceUtils::collectImageSequenceFrames(inputPath);
    for (const ImageSequenceFrameInfo &frame : resolvedFrames) {
        source.resolvedFramePaths.emplace(frame.frameIndex, pathFromQString(frame.path));
    }

    for (int sequenceIndex = 0; sequenceIndex < source.totalFrameCount; ++sequenceIndex) {
        const int frameIndex = frameIndexAt(source, static_cast<std::size_t>(sequenceIndex));
        if (!source.resolvedFramePaths.contains(frameIndex)) {
            if (errorMessage) {
                *errorMessage = std::format("Missing input frame {} for '{}'", frameIndex, sourceOptions.input.string());
            }
            return false;
        }
    }

    return true;
}

int SliceImageLoader::frameIndexAt(const SourceState &source, std::size_t sequenceIndex)
{
    if (!source.sequence) {
        return source.start;
    }
    const int offset = static_cast<int>(sequenceIndex) * source.step * source.direction;
    return source.start + offset;
}

SliceImageLoader::FrameSlot SliceImageLoader::buildFrameSlot(const SourceState &source, std::size_t sequenceIndex)
{
    FrameSlot frame;
    frame.sequenceIndex = sequenceIndex;
    frame.frameIndex = frameIndexAt(source, sequenceIndex);
    if (source.sequence) {
        const auto it = source.resolvedFramePaths.find(frame.frameIndex);
        frame.path = it != source.resolvedFramePaths.end()
            ? it->second
            : pathFromQString(ImageSequenceUtils::buildFramePath(source.directory, source.prefix, source.digitCount, source.suffix, frame.frameIndex));
    }
    else {
        frame.path = source.input;
    }
    return frame;
}

SliceImageLoader::FrameSlot* SliceImageLoader::ensureFrameForSequenceIndex(SourceState &source, std::size_t sequenceIndex)
{
    if (sequenceIndex >= static_cast<std::size_t>(std::max(0, source.totalFrameCount))) {
        return nullptr;
    }

    auto it = std::ranges::find(source.frames, sequenceIndex, &FrameSlot::sequenceIndex);
    if (it != source.frames.end()) {
        return &*it;
    }

    source.frames.push_back(buildFrameSlot(source, sequenceIndex));
    return &source.frames.back();
}

std::optional<std::size_t> SliceImageLoader::sequenceIndexForFrame(const SourceState &source, int frameIndex)
{
    if (source.totalFrameCount <= 0) {
        return std::nullopt;
    }
    if (!source.sequence) {
        return std::size_t{ 0 };
    }

    const int distance = (frameIndex - source.start) * source.direction;
    if (distance < 0 || distance % source.step != 0) {
        return std::nullopt;
    }

    const std::size_t index = static_cast<std::size_t>(distance / source.step);
    if (index >= static_cast<std::size_t>(source.totalFrameCount)) {
        return std::nullopt;
    }
    return index;
}

SliceImageLoader::VerificationResult SliceImageLoader::verifySource(const SourceOptions &sourceOptions,
    VerificationProgressCallback progressCallback,
    void *progressUserData,
    int sizeWarningPercent)
{
    VerificationResult result;
    std::string error;
    std::deque<FrameSlot> frames = buildFrameList(sourceOptions, &error);
    if (frames.empty()) {
        result.error.path = sourceOptions.input;
        result.error.message = error.empty() ? "No input frames were found" : std::move(error);
        result.errors.push_back(result.error);
        return result;
    }

    result.totalFrames = static_cast<int>(frames.size());
    const int warningPercent = std::clamp(sizeWarningPercent, 0, 100);
    std::vector<VerificationFileSize> fileSizes(frames.size());
    Wuffs::DecodeContext context;
    bool haveReferenceDimensions = false;
    for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        const FrameSlot &frame = frames[frameIndex];
        std::error_code fileSizeError;
        const std::uintmax_t fileSize = std::filesystem::file_size(frame.path, fileSizeError);
        if (!fileSizeError) {
            fileSizes[frameIndex] = { fileSize, true };
        }

        Wuffs::ImageInfo info;
        error.clear();
        if (!Wuffs::readImageInfo(frame.path, info, context, &error)) {
            VerificationError verificationError;
            verificationError.path = frame.path;
            verificationError.message = error.empty() ? "Could not read image header" : std::move(error);
            if (result.errors.empty()) {
                result.error = verificationError;
            }
            result.errors.push_back(std::move(verificationError));
            ++result.verifiedFrames;
            if (progressCallback) {
                progressCallback(result.verifiedFrames, result.totalFrames, result.width, result.height, progressUserData);
            }
            continue;
        }

        if (!haveReferenceDimensions) {
            result.width = info.width;
            result.height = info.height;
            haveReferenceDimensions = true;
        }
        else if (info.width != result.width || info.height != result.height) {
            VerificationError verificationError;
            verificationError.path = frame.path;
            verificationError.message = std::format("Image dimensions {}x{} do not match expected {}x{}", info.width, info.height, result.width, result.height);
            if (result.errors.empty()) {
                result.error = verificationError;
            }
            result.errors.push_back(std::move(verificationError));
        }

        ++result.verifiedFrames;
        if (progressCallback) {
            progressCallback(result.verifiedFrames, result.totalFrames, result.width, result.height, progressUserData);
        }
    }

    if (warningPercent > 0 && frames.size() >= 2) {
        for (std::size_t index = 0; index < frames.size(); ++index) {
            if (!fileSizes[index].known) {
                continue;
            }

            std::uintmax_t referenceBytes = 0;
            if (index > 0 && fileSizes[index - 1].known && isSmallerByPercent(fileSizes[index].bytes, fileSizes[index - 1].bytes, warningPercent)) {
                referenceBytes = std::max(referenceBytes, fileSizes[index - 1].bytes);
            }
            if (index + 1 < frames.size() && fileSizes[index + 1].known && isSmallerByPercent(fileSizes[index].bytes, fileSizes[index + 1].bytes, warningPercent)) {
                referenceBytes = std::max(referenceBytes, fileSizes[index + 1].bytes);
            }

            if (referenceBytes > 0) {
                VerificationError warning;
                warning.path = frames[index].path;
                warning.message = std::format("Image file is at least {}% smaller than a neighboring file ({} vs {})",
                    warningPercent,
                    formatFileSize(fileSizes[index].bytes),
                    formatFileSize(referenceBytes));
                result.warnings.push_back(std::move(warning));
            }
        }
    }

    result.ok = result.errors.empty();
    return result;
}

void SliceImageLoader::startWorkers()
{
    std::lock_guard lock(m_mutex);
    if (!m_loaderSlots.empty()) {
        return;
    }

    m_stopping = false;
    const int slotCount = std::max(1, m_effectiveLoadingThreadCount);
    m_loaderSlots.reserve(static_cast<std::size_t>(slotCount));
    for (int i = 0; i < slotCount; ++i) {
        auto slot = std::make_unique<LoaderSlot>();
        m_loaderSlots.push_back(std::move(slot));
    }

    for (std::size_t i = 0; i < m_loaderSlots.size(); ++i) {
        m_loaderSlots[i]->worker = std::thread(&SliceImageLoader::workerLoop, this, i);
    }
}

void SliceImageLoader::stopWorkers()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
    }

    for (const std::unique_ptr<LoaderSlot> &slot : m_loaderSlots) {
        {
            std::lock_guard slotLock(slot->mutex);
            slot->stopping = true;
            slot->state = LoaderSlotState::Stopping;
        }
        slot->workAvailable.notify_all();
    }

    for (const std::unique_ptr<LoaderSlot> &slot : m_loaderSlots) {
        if (slot->worker.joinable()) {
            slot->worker.join();
        }
    }
}

void SliceImageLoader::scheduleFrames()
{
    {
        std::lock_guard lock(m_mutex);
        scheduleFramesLocked();
    }

    for (const std::unique_ptr<LoaderSlot> &slot : m_loaderSlots) {
        slot->workAvailable.notify_all();
    }
}

void SliceImageLoader::scheduleFramesLocked()
{
    if (m_paused || m_stopping || m_sources.empty() || m_loaderSlots.empty()) {
        return;
    }

    for (std::size_t slotIndex = 0; slotIndex < m_loaderSlots.size(); ++slotIndex) {
        LoaderSlot &slot = *m_loaderSlots[slotIndex];
        int bufferIndex = 0;
        {
            std::lock_guard slotLock(slot.mutex);
            if (slot.state != LoaderSlotState::Idle) {
                continue;
            }

            bufferIndex = slot.decodeBufferIndex;
            const DecodeBuffer &buffer = slot.buffers[bufferIndex];
            if (buffer.state != DecodeBuffer::State::Empty && buffer.state != DecodeBuffer::State::Uploaded && buffer.state != DecodeBuffer::State::Failed) {
                continue;
            }
        }

        const std::optional<std::pair<std::size_t, std::size_t>> job = nextFrameToScheduleLocked(slotIndex);
        if (!job) {
            continue;
        }

        SourceState &source = m_sources[job->first];
        FrameSlot *frame = frameForSequenceIndex(source, job->second);
        if (!frame) {
            continue;
        }
        std::lock_guard slotLock(slot.mutex);
        if (slot.state != LoaderSlotState::Idle || slot.decodeBufferIndex != bufferIndex) {
            continue;
        }

        DecodeBuffer &buffer = slot.buffers[bufferIndex];
        buffer = DecodeBuffer{};
        buffer.sourceIndex = job->first;
        buffer.sequenceIndex = frame->sequenceIndex;
        buffer.frameIndex = frame->frameIndex;
        buffer.path = frame->path;
        buffer.state = DecodeBuffer::State::Decoding;

        frame->queued = true;
        frame->decoding = true;
        slot.state = LoaderSlotState::DecodeQueued;
        slot.decodeBufferIndex = 1 - slot.decodeBufferIndex;
    }
}

void SliceImageLoader::workerLoop(std::size_t workerIndex)
{
    if (workerIndex >= m_loaderSlots.size()) {
        return;
    }

    LoaderSlot &slot = *m_loaderSlots[workerIndex];
    while (true) {
        int bufferIndex = -1;
        {
            std::unique_lock slotLock(slot.mutex);
            slot.workAvailable.wait(slotLock, [&slot]() {
                return slot.stopping || slot.state == LoaderSlotState::DecodeQueued;
            });
            if (slot.stopping) {
                return;
            }

            bufferIndex = 1 - slot.decodeBufferIndex;
            slot.state = LoaderSlotState::Decoding;
            slot.buffers[bufferIndex].state = DecodeBuffer::State::Decoding;
        }

        DecodeBuffer &buffer = slot.buffers[bufferIndex];
        std::string error;
        const bool ok = Wuffs::decodeFile(buffer.path, buffer.image, slot.decodeContext, &error);

        const std::size_t sourceIndex = buffer.sourceIndex;
        const std::size_t sequenceIndex = buffer.sequenceIndex;
        const std::string errorMessage = ok ? std::string{} : (error.empty() ? "Could not decode image" : error);
        {
            std::lock_guard slotLock(slot.mutex);
            if (slot.stopping) {
                return;
            }
            buffer.error = errorMessage;
            buffer.state = ok ? DecodeBuffer::State::Decoded : DecodeBuffer::State::Failed;
            slot.uploadBufferIndex = bufferIndex;
            slot.state = ok ? LoaderSlotState::Decoded : LoaderSlotState::Failed;
        }

        {
            std::lock_guard globalLock(m_mutex);
            if (sourceIndex < m_sources.size()) {
                SourceState &source = m_sources[sourceIndex];
                FrameSlot *frame = frameForSequenceIndex(source, sequenceIndex);
                if (frame) {
                    frame->queued = false;
                    frame->decoding = false;
                    if (ok) {
                        frame->decoded = true;
                        frame->failed = false;
                        frame->error.clear();
                    }
                    else {
                        frame->decoded = false;
                        frame->failed = true;
                        frame->error = errorMessage;
                        m_failures.push_back(VerificationError{ frame->path, frame->error });
                    }
                    if (!frame->loadedOnce) {
                        source.loadedFrameCount = std::min(source.loadedFrameCount + 1, source.totalFrameCount);
                        frame->loadedOnce = true;
                    }
                }
            }
        }

        m_frameAvailable.notify_all();
    }
}

void SliceImageLoader::ensureGlResources()
{
    if (m_loaderSlots.empty()) {
        startWorkers();
    }

    for (const std::unique_ptr<LoaderSlot> &slot : m_loaderSlots) {
        if (slot->pbo == 0) {
            glGenBuffers(1, &slot->pbo);
        }
        if (slot->textureId == 0) {
            glGenTextures(1, &slot->textureId);
            glBindTexture(GL_TEXTURE_2D, slot->textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
}

void SliceImageLoader::uploadDecodedSlots(int frameIndex, bool forceCurrentFrame)
{
    (void)forceCurrentFrame;

    for (const std::unique_ptr<LoaderSlot> &slotPtr : m_loaderSlots) {
        LoaderSlot &slot = *slotPtr;
        int bufferIndex = -1;
        {
            std::lock_guard slotLock(slot.mutex);
            if (slot.state != LoaderSlotState::Decoded || slot.uploadBufferIndex < 0) {
                continue;
            }
            bufferIndex = slot.uploadBufferIndex;
        }

        DecodeBuffer &buffer = slot.buffers[bufferIndex];
        {
            std::lock_guard lock(m_mutex);
            if (!canOverwriteSlotTextureLocked(slot)) {
                continue;
            }
        }

        {
            std::lock_guard slotLock(slot.mutex);
            if (slot.state != LoaderSlotState::Decoded || slot.uploadBufferIndex != bufferIndex) {
                continue;
            }
            slot.buffers[bufferIndex].state = DecodeBuffer::State::Uploading;
        }

        uploadBufferToTexture(slot, buffer);

        {
            std::lock_guard lock(m_mutex);
            if (buffer.sourceIndex < m_sources.size()) {
                SourceState &source = m_sources[buffer.sourceIndex];
                FrameSlot *frame = frameForSequenceIndex(source, buffer.sequenceIndex);
                if (frame) {
                    slot.textureSourceIndex = buffer.sourceIndex;
                    slot.textureSequenceIndex = buffer.sequenceIndex;
                    slot.textureFrameIndex = buffer.frameIndex;
                    slot.textureAssigned = true;
                    frame->textureId = slot.textureId;
                    frame->uploaded = true;
                    frame->decoded = true;
                    frame->failed = false;
                    applyRenderOptions(source.currentLayer, source.renderOptions);
                    if (frame->frameIndex == frameIndex || !source.sequence) {
                        source.currentLayer.textureId = slot.textureId;
                        source.currentLayer.width = buffer.image.width;
                        source.currentLayer.height = buffer.image.height;
                        source.currentLayer.textureReady = true;
                    }
                }
            }
        }

        {
            std::lock_guard slotLock(slot.mutex);
            buffer.state = DecodeBuffer::State::Uploaded;
            slot.uploadBufferIndex = -1;
            slot.state = LoaderSlotState::Idle;
        }
        slot.workAvailable.notify_all();
    }
}

void SliceImageLoader::uploadBufferToTexture(LoaderSlot &slot, DecodeBuffer &buffer)
{
    const std::size_t bytes = buffer.image.byteSize();
    if (bytes == 0 || buffer.image.pixels.empty()) {
        return;
    }

    const GlPixelFormat pixelFormat = glPixelFormat(buffer.image.format);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, slot.pbo);
    if (slot.pboBytes != bytes) {
        glBufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_DRAW);
        slot.pboBytes = bytes;
    }
    else {
        glBufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_DRAW);
    }

    void *mapped = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER,
        0,
        static_cast<GLsizeiptr>(bytes),
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
    if (mapped) {
        std::memcpy(mapped, buffer.image.pixels.data(), bytes);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }

    glBindTexture(GL_TEXTURE_2D, slot.textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (slot.textureWidth != buffer.image.width ||
        slot.textureHeight != buffer.image.height ||
        slot.textureFormat != buffer.image.format) {
        glTexImage2D(GL_TEXTURE_2D,
            0,
            pixelFormat.internalFormat,
            buffer.image.width,
            buffer.image.height,
            0,
            pixelFormat.format,
            GL_UNSIGNED_BYTE,
            nullptr);
        slot.textureWidth = buffer.image.width;
        slot.textureHeight = buffer.image.height;
        slot.textureFormat = buffer.image.format;
    }

    glTexSubImage2D(GL_TEXTURE_2D,
        0,
        0,
        0,
        buffer.image.width,
        buffer.image.height,
        pixelFormat.format,
        GL_UNSIGNED_BYTE,
        nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    slot.width = buffer.image.width;
    slot.height = buffer.image.height;
    slot.format = buffer.image.format;
}

void SliceImageLoader::clearFrameStateLocked(FrameSlot &frame)
{
    frame.textureId = 0;
    frame.queued = false;
    frame.decoding = false;
    frame.decoded = false;
    frame.uploaded = false;
    frame.loadedOnce = false;
    frame.failed = false;
    frame.error.clear();
}

const SliceImageLoader::LoaderSlot* SliceImageLoader::textureOwnerLocked(std::size_t sourceIndex, std::size_t sequenceIndex, unsigned int textureId) const
{
    if (textureId == 0) {
        return nullptr;
    }

    for (const std::unique_ptr<LoaderSlot> &slot : m_loaderSlots) {
        if (slot->textureAssigned &&
            slot->textureId == textureId &&
            slot->textureSourceIndex == sourceIndex &&
            slot->textureSequenceIndex == sequenceIndex) {
            return slot.get();
        }
    }
    return nullptr;
}

bool SliceImageLoader::frameTextureIsCurrentLocked(std::size_t sourceIndex, const FrameSlot &frame) const
{
    return textureOwnerLocked(sourceIndex, frame.sequenceIndex, frame.textureId) != nullptr;
}

bool SliceImageLoader::canOverwriteSlotTextureLocked(const LoaderSlot &slot) const
{
    if (!slot.textureAssigned) {
        return true;
    }
    if (m_releasedFrameSequenceIndex && slot.textureSequenceIndex <= *m_releasedFrameSequenceIndex) {
        return true;
    }
    return false;
}

SliceImageLoader::FrameSlot* SliceImageLoader::pendingFrameForSequenceIndex(SourceState &source, std::size_t sourceIndex, std::size_t sequenceIndex) const
{
    FrameSlot *frame = ensureFrameForSequenceIndex(source, sequenceIndex);
    if (!frame || frame->failed || frame->queued || frame->decoding) {
        return nullptr;
    }
    if ((frame->decoded || frame->uploaded) && frameTextureIsCurrentLocked(sourceIndex, *frame)) {
        return nullptr;
    }
    if (frameHasPendingSlotLocked(sourceIndex, sequenceIndex)) {
        return nullptr;
    }
    return frame;
}

void SliceImageLoader::releaseFramesUpToSequenceIndexLocked(std::size_t sequenceIndex)
{
    if (m_releasedFrameSequenceIndex && sequenceIndex <= *m_releasedFrameSequenceIndex) {
        return;
    }
    m_releasedFrameSequenceIndex = sequenceIndex;

    for (std::size_t sourceIndex = 0; sourceIndex < m_sources.size(); ++sourceIndex) {
        SourceState &source = m_sources[sourceIndex];
        for (auto frameIt = source.frames.begin(); frameIt != source.frames.end();) {
            FrameSlot &frame = *frameIt;
            if (frame.sequenceIndex <= sequenceIndex && frame.frameIndex != m_currentFrameIndex) {
                if (frame.textureId != 0) {
                    for (const std::unique_ptr<LoaderSlot> &slot : m_loaderSlots) {
                        if (slot->textureAssigned &&
                            slot->textureId == frame.textureId &&
                            slot->textureSourceIndex == sourceIndex &&
                            slot->textureSequenceIndex == frame.sequenceIndex) {
                            slot->textureAssigned = false;
                            break;
                        }
                    }
                }
                frameIt = source.frames.erase(frameIt);
            }
            else {
                ++frameIt;
            }
        }
    }
}

SliceImageLoader::FrameSlot* SliceImageLoader::frameForSequenceIndex(SourceState &source, std::size_t sequenceIndex)
{
    auto it = std::ranges::find(source.frames, sequenceIndex, &FrameSlot::sequenceIndex);
    return it == source.frames.end() ? nullptr : &*it;
}

const SliceImageLoader::FrameSlot* SliceImageLoader::frameForSequenceIndex(const SourceState &source, std::size_t sequenceIndex) const
{
    auto it = std::ranges::find(source.frames, sequenceIndex, &FrameSlot::sequenceIndex);
    return it == source.frames.end() ? nullptr : &*it;
}

SliceImageLoader::FrameSlot* SliceImageLoader::frameForIndex(SourceState &source, int frameIndex)
{
    auto it = std::ranges::find_if(source.frames, [frameIndex](const FrameSlot &frame) {
        return frame.frameIndex == frameIndex;
    });
    if (it != source.frames.end()) {
        return &*it;
    }
    if (const std::optional<std::size_t> sequenceIndex = sequenceIndexForFrame(source, frameIndex)) {
        return ensureFrameForSequenceIndex(source, *sequenceIndex);
    }
    if (source.frames.size() == 1) {
        return &source.frames.front();
    }
    return nullptr;
}

const SliceImageLoader::FrameSlot* SliceImageLoader::frameForIndex(const SourceState &source, int frameIndex) const
{
    auto it = std::ranges::find_if(source.frames, [frameIndex](const FrameSlot &frame) {
        return frame.frameIndex == frameIndex;
    });
    if (it != source.frames.end()) {
        return &*it;
    }
    if (source.frames.size() == 1) {
        return &source.frames.front();
    }
    return nullptr;
}

const SliceLayer* SliceImageLoader::currentLayerForEye(bool rightEye) const
{
    std::lock_guard lock(m_mutex);
    if (m_sources.empty()) {
        return nullptr;
    }
    if (rightEye && m_sources.size() > 1) {
        return &m_sources[1].currentLayer;
    }
    return &m_sources.front().currentLayer;
}

std::optional<std::size_t> SliceImageLoader::sequenceIndexForFrameLocked(int frameIndex) const
{
    if (m_sources.empty()) {
        return std::nullopt;
    }

    return sequenceIndexForFrame(m_sources.front(), frameIndex);
}

bool SliceImageLoader::frameHasPendingSlotLocked(std::size_t sourceIndex, std::size_t sequenceIndex) const
{
    for (const std::unique_ptr<LoaderSlot> &slot : m_loaderSlots) {
        std::lock_guard slotLock(slot->mutex);
        for (const DecodeBuffer &buffer : slot->buffers) {
            if (buffer.sourceIndex == sourceIndex &&
                buffer.sequenceIndex == sequenceIndex &&
                buffer.state != DecodeBuffer::State::Empty &&
                buffer.state != DecodeBuffer::State::Uploaded &&
                buffer.state != DecodeBuffer::State::Failed) {
                return true;
            }
        }
    }
    return false;
}

std::optional<std::pair<std::size_t, std::size_t>> SliceImageLoader::nextFrameToScheduleLocked(std::size_t slotIndex)
{
    if (m_sources.empty()) {
        return std::nullopt;
    }

    const std::size_t totalFrames = static_cast<std::size_t>(std::max(0, m_sources.front().totalFrameCount));
    if (totalFrames == 0) {
        return std::nullopt;
    }

    const std::size_t sourceCount = m_sources.size();
    const std::size_t maxLead = std::max<std::size_t>(sourceCount * m_loaderSlots.size() * 2u, sourceCount);
    const std::size_t startSequence = std::min(m_nextScheduleSequenceIndex, totalFrames - 1u);

    for (std::size_t offset = 0; offset < maxLead && startSequence + offset < totalFrames; ++offset) {
        const std::size_t sequenceIndex = startSequence + offset;
        if (m_releasedFrameSequenceIndex && sequenceIndex <= *m_releasedFrameSequenceIndex) {
            continue;
        }
        for (std::size_t sourceOffset = 0; sourceOffset < sourceCount; ++sourceOffset) {
            const std::size_t sourceIndex = (slotIndex + sourceOffset) % sourceCount;
            if (pendingFrameForSequenceIndex(m_sources[sourceIndex], sourceIndex, sequenceIndex)) {
                return std::make_pair(sourceIndex, sequenceIndex);
            }
        }
    }

    return std::nullopt;
}

} // namespace CSlice

