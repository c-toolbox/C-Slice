/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "slicecaptureoutput.h"

#include <sgct/config.h>
#include <sgct/window.h>

#include <algorithm>
#include <cstring>

namespace CSlice {

SliceCaptureOutput::SliceCaptureOutput() = default;

SliceCaptureOutput::~SliceCaptureOutput()
{
    cleanup();
}

void SliceCaptureOutput::configure(std::filesystem::path output, GLenum pixelFormat, int bytesPerPixel, int captureSlotCount)
{
    cleanup();
    m_output = std::move(output);
    m_pixelFormat = pixelFormat;
    m_bytesPerPixel = bytesPerPixel;
    m_captureSlotCount = std::max(1, captureSlotCount);
    m_active = true;
}

bool SliceCaptureOutput::initializeMovie(const sgct::Window& window, const EncoderOptions& options)
{
    const sgct::ivec2 resolution = window.framebufferResolution();
    if (resolution.x <= 0 || resolution.y <= 0) {
        m_active = false;
        return false;
    }

    m_resolution = resolution;
    m_encoder = std::make_unique<SliceEncoder>();
    m_encoder->setUseOnlyIframes(options.useOnlyIframes);
    m_encoder->setGopSize(options.gopSize);
    m_encoder->setDesiredCodec(options.codec);
    m_encoder->setHardwareEncoderName(options.hardwareEncoderName);
    m_encoder->setFrameRate(options.frameRateNum, options.frameRateDen);
    m_encoder->setInputFrameRate(options.inputFrameRateNum, options.inputFrameRateDen);
    m_encoder->setConstantQuality(options.constantQuality);
    m_encoder->setPreset(options.preset);
    m_encoder->setLibxTune(options.libxTune);
    m_encoder->setNvencTune(options.nvencTune);
    m_encoder->setNvencHardwareFrames(options.nvencHardwareFrames);
    m_encoder->setEncodingBitDepth(options.encodingBitDepth);
    m_encoder->setParameterFile(options.parameterFile);
    m_encoder->setInputPixelFormat(AV_PIX_FMT_BGR24);
    if (!m_encoder->init(m_output, resolution.x, resolution.y, options.pixrate, options.maxEncoderThreads)) {
        m_encoder.reset();
        m_active = false;
        return false;
    }
    return true;
}

bool SliceCaptureOutput::blitReadback(sgct::Window& window, int frameIndex, int sequenceIndex)
{
    const GLuint sourceTexture = window.frameBufferTextureEye(sgct::Eye::MonoOrLeft);
    if (sourceTexture == 0) {
        return false;
    }

    const sgct::ivec2 resolution = window.framebufferResolution();
    if (resolution.x <= 0 || resolution.y <= 0) {
        return false;
    }

    window.makeOpenGLContextCurrent();
    if (m_readbackSlots.empty()) {
        m_readbackSlots.resize(static_cast<std::size_t>(m_captureSlotCount));
    }

    ReadbackSlot* slot = nullptr;
    for (ReadbackSlot& candidate : m_readbackSlots) {
        if (!candidate.pending) {
            slot = &candidate;
            break;
        }
    }
    if (!slot || !ensureReadbackSlotPbo(*slot, resolution) || !ensureCaptureSlotTarget(*slot, resolution)) {
        return false;
    }

    if (slot->fence) {
        glDeleteSync(slot->fence);
        slot->fence = nullptr;
    }

    GLint previousReadFramebuffer = 0;
    GLint previousDrawFramebuffer = 0;
    GLint previousViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    if (slot->blitFramebuffer == 0) {
        glGenFramebuffers(1, &slot->blitFramebuffer);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, slot->blitFramebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sourceTexture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot->captureFramebuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0,
        0,
        resolution.x,
        resolution.y,
        0,
        0,
        resolution.x,
        resolution.y,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(slot->bytes), nullptr, GL_STREAM_READ);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, resolution.x, resolution.y, m_pixelFormat, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    slot->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    slot->pending = true;
    slot->frameIndex = frameIndex;
    slot->sequenceIndex = sequenceIndex;
    return true;
}

bool SliceCaptureOutput::issueReadback(sgct::Window& window, int frameIndex, int sequenceIndex)
{
    const GLuint texture = window.frameBufferTextureEye(sgct::Eye::MonoOrLeft);
    if (texture == 0) {
        return false;
    }

    const sgct::ivec2 resolution = window.framebufferResolution();
    if (resolution.x <= 0 || resolution.y <= 0) {
        return false;
    }

    window.makeOpenGLContextCurrent();
    if (m_readbackSlots.empty()) {
        m_readbackSlots.resize(static_cast<std::size_t>(m_captureSlotCount));
    }

    ReadbackSlot* slot = nullptr;
    for (ReadbackSlot& candidate : m_readbackSlots) {
        if (!candidate.pending) {
            slot = &candidate;
            break;
        }
    }
    if (!slot || !ensureReadbackSlotPbo(*slot, resolution)) {
        return false;
    }

    if (slot->fence) {
        glDeleteSync(slot->fence);
        slot->fence = nullptr;
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(slot->bytes), nullptr, GL_STREAM_READ);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, m_pixelFormat, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    slot->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    slot->pending = true;
    slot->frameIndex = frameIndex;
    slot->sequenceIndex = sequenceIndex;
    return true;
}

bool SliceCaptureOutput::pump(sgct::Window& window, bool block, bool& submittedFrame)
{
    submittedFrame = false;
    if (!m_active) {
        return true;
    }

    if (!collectReadyReadbacks(window, block)) {
        return false;
    }

    if (!m_encoder) {
        if (!m_completedFrames.empty()) {
            m_completedFrames.clear();
            submittedFrame = true;
        }
        return true;
    }

    auto it = m_completedFrames.find(m_nextSequenceToEncode);
    if (it == m_completedFrames.end()) {
        return true;
    }

    if (!m_encoder->addRgbaPixels(it->second.data())) {
        return false;
    }

    m_completedFrames.erase(it);
    ++m_nextSequenceToEncode;
    submittedFrame = true;
    return true;
}

bool SliceCaptureOutput::finish(sgct::Window& window)
{
    while (m_encoder && (pendingReadbackSlotCount() > 0 || !m_completedFrames.empty())) {
        bool submittedFrame = false;
        if (!pump(window, true, submittedFrame)) {
            return false;
        }
        if (!submittedFrame && pendingReadbackSlotCount() == 0) {
            return m_completedFrames.empty();
        }
    }
    return true;
}

void SliceCaptureOutput::cleanup()
{
    for (ReadbackSlot& slot : m_readbackSlots) {
        destroyReadbackSlot(slot);
    }
    m_readbackSlots.clear();
    m_completedFrames.clear();
    m_encoder.reset();
    m_pixels.clear();
    m_nextSequenceToEncode = 0;
    m_active = false;
}

bool SliceCaptureOutput::active() const
{
    return m_active;
}

void SliceCaptureOutput::setActive(bool active)
{
    m_active = active;
}

bool SliceCaptureOutput::hasEncoder() const
{
    return m_encoder != nullptr;
}

bool SliceCaptureOutput::ok() const
{
    return !m_encoder || m_encoder->isOk();
}

bool SliceCaptureOutput::usingNvencHardwareFrames() const
{
    return m_encoder && m_encoder->usingNvencHardwareFrames();
}

bool SliceCaptureOutput::nvencHardwareFramesRequested() const
{
    return m_encoder && m_encoder->nvencHardwareFramesRequestedByUser();
}

bool SliceCaptureOutput::readbackQueueFull() const
{
    return pendingReadbackSlotCount() >= static_cast<std::size_t>(m_captureSlotCount);
}

int SliceCaptureOutput::encodedFrameCount() const
{
    return m_nextSequenceToEncode;
}

const std::filesystem::path& SliceCaptureOutput::outputPath() const
{
    return m_output;
}

GLenum SliceCaptureOutput::pixelFormat() const
{
    return m_pixelFormat;
}

int SliceCaptureOutput::bytesPerPixel() const
{
    return m_bytesPerPixel;
}

sgct::ivec2 SliceCaptureOutput::resolution() const
{
    return m_resolution;
}

void SliceCaptureOutput::setResolution(sgct::ivec2 resolution)
{
    m_resolution = resolution;
}

std::vector<unsigned char>& SliceCaptureOutput::pixels()
{
    return m_pixels;
}

bool SliceCaptureOutput::ensureReadbackSlotPbo(ReadbackSlot& slot, sgct::ivec2 resolution)
{
    const std::size_t bytes = static_cast<std::size_t>(resolution.x) *
        static_cast<std::size_t>(resolution.y) * static_cast<std::size_t>(m_bytesPerPixel);
    if (bytes == 0) {
        return false;
    }

    m_resolution = resolution;
    slot.resolution = resolution;
    if (slot.pbo == 0) {
        glGenBuffers(1, &slot.pbo);
    }
    if (slot.bytes != bytes) {
        slot.bytes = bytes;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(slot.bytes), nullptr, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    return slot.pbo != 0;
}

bool SliceCaptureOutput::ensureCaptureSlotTarget(ReadbackSlot& slot, sgct::ivec2 resolution)
{
    if (slot.captureTexture == 0) {
        glGenTextures(1, &slot.captureTexture);
    }
    glBindTexture(GL_TEXTURE_2D, slot.captureTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
        0,
        m_pixelFormat == GL_BGR ? GL_RGB8 : GL_RGBA8,
        resolution.x,
        resolution.y,
        0,
        m_pixelFormat,
        GL_UNSIGNED_BYTE,
        nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (slot.captureFramebuffer == 0) {
        glGenFramebuffers(1, &slot.captureFramebuffer);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, slot.captureFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.captureTexture, 0);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return complete;
}

bool SliceCaptureOutput::readbackSlotReady(ReadbackSlot& slot, bool block)
{
    if (!slot.fence) {
        return true;
    }

    const GLenum waitResult = glClientWaitSync(slot.fence,
        block ? GL_SYNC_FLUSH_COMMANDS_BIT : 0,
        block ? GL_TIMEOUT_IGNORED : 0);
    if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED) {
        glDeleteSync(slot.fence);
        slot.fence = nullptr;
        return true;
    }
    return false;
}

bool SliceCaptureOutput::collectReadyReadbacks(sgct::Window& window, bool block)
{
    window.makeOpenGLContextCurrent();
    for (ReadbackSlot& slot : m_readbackSlots) {
        if (!slot.pending) {
            continue;
        }
        if (!readbackSlotReady(slot, block)) {
            continue;
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
        const auto* pixels = static_cast<const unsigned char*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER,
            0,
            static_cast<GLsizeiptr>(slot.bytes),
            GL_MAP_READ_BIT));
        if (!pixels) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            return false;
        }

        std::vector<unsigned char>& completed = m_completedFrames[slot.sequenceIndex];
        completed.resize(slot.bytes);
        std::memcpy(completed.data(), pixels, slot.bytes);

        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        slot.pending = false;
        slot.frameIndex = -1;
        slot.sequenceIndex = -1;
    }
    return true;
}

void SliceCaptureOutput::destroyReadbackSlot(ReadbackSlot& slot)
{
    if (slot.fence) {
        glDeleteSync(slot.fence);
        slot.fence = nullptr;
    }
    if (slot.pbo != 0) {
        glDeleteBuffers(1, &slot.pbo);
        slot.pbo = 0;
    }
    if (slot.captureFramebuffer != 0) {
        glDeleteFramebuffers(1, &slot.captureFramebuffer);
        slot.captureFramebuffer = 0;
    }
    if (slot.blitFramebuffer != 0) {
        glDeleteFramebuffers(1, &slot.blitFramebuffer);
        slot.blitFramebuffer = 0;
    }
    if (slot.captureTexture != 0) {
        glDeleteTextures(1, &slot.captureTexture);
        slot.captureTexture = 0;
    }
    slot.bytes = 0;
    slot.pending = false;
    slot.frameIndex = -1;
    slot.sequenceIndex = -1;
}

std::size_t SliceCaptureOutput::pendingReadbackSlotCount() const
{
    return static_cast<std::size_t>(std::ranges::count_if(m_readbackSlots, [](const ReadbackSlot& slot) {
        return slot.pending;
    }));
}

} // namespace CSlice
