/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SPLITCAPTUREOUTPUT_H
#define CSLICE_SPLITCAPTUREOUTPUT_H

#include "sliceencoder.h"

#include <sgct/math.h>
#include <sgct/opengl.h>

#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace sgct { class Window; }

namespace CSlice {

class SliceCaptureOutput {
public:
    struct EncoderOptions {
        AVCodecID codec = AV_CODEC_ID_H264;
        std::string hardwareEncoderName;
        int frameRateNum = 1;
        int frameRateDen = 30;
        int inputFrameRateNum = 30;
        int inputFrameRateDen = 1;
        int constantQuality = 23;
        std::string preset = "ultrafast";
        std::string libxTune = "fastdecode";
        std::string nvencTune = "hq";
        bool nvencHardwareFrames = false;
        int encodingBitDepth = 8;
        std::filesystem::path parameterFile;
        int pixrate = 6;
        unsigned int maxEncoderThreads = 16;
        bool useOnlyIframes = false;
        int gopSize = 1;
    };

    SliceCaptureOutput();
    SliceCaptureOutput(const SliceCaptureOutput&) = delete;
    SliceCaptureOutput& operator=(const SliceCaptureOutput&) = delete;
    SliceCaptureOutput(SliceCaptureOutput&&) noexcept = default;
    SliceCaptureOutput& operator=(SliceCaptureOutput&&) noexcept = default;
    ~SliceCaptureOutput();

    void configure(std::filesystem::path output, GLenum pixelFormat, int bytesPerPixel, int captureSlotCount);
    bool initializeMovie(const sgct::Window& window, const EncoderOptions& options);

    bool issueReadback(sgct::Window& window, int frameIndex, int sequenceIndex);
    bool blitReadback(sgct::Window& window, int frameIndex, int sequenceIndex);
    bool pump(sgct::Window& window, bool block, bool& submittedFrame);
    bool finish(sgct::Window& window);
    void cleanup();

    bool active() const;
    void setActive(bool active);
    bool hasEncoder() const;
    bool ok() const;
    bool usingNvencHardwareFrames() const;
    bool nvencHardwareFramesRequested() const;
    bool readbackQueueFull() const;
    int encodedFrameCount() const;

    const std::filesystem::path& outputPath() const;
    GLenum pixelFormat() const;
    int bytesPerPixel() const;
    sgct::ivec2 resolution() const;
    void setResolution(sgct::ivec2 resolution);
    std::vector<unsigned char>& pixels();

private:
    struct ReadbackSlot {
        GLuint pbo = 0;
        GLuint captureTexture = 0;
        GLuint captureFramebuffer = 0;
        GLuint blitFramebuffer = 0;
        std::size_t bytes = 0;
        sgct::ivec2 resolution;
        GLsync fence = nullptr;
        bool pending = false;
        int frameIndex = -1;
        int sequenceIndex = -1;
    };

    bool ensureReadbackSlotPbo(ReadbackSlot& slot, sgct::ivec2 resolution);
    bool ensureCaptureSlotTarget(ReadbackSlot& slot, sgct::ivec2 resolution);
    bool readbackSlotReady(ReadbackSlot& slot, bool block);
    bool collectReadyReadbacks(sgct::Window& window, bool block);
    void destroyReadbackSlot(ReadbackSlot& slot);
    std::size_t pendingReadbackSlotCount() const;

    std::filesystem::path m_output;
    std::unique_ptr<SliceEncoder> m_encoder;
    std::vector<unsigned char> m_pixels;
    sgct::ivec2 m_resolution = sgct::ivec2{ 0, 0 };
    std::vector<ReadbackSlot> m_readbackSlots;
    std::map<int, std::vector<unsigned char>> m_completedFrames;
    GLenum m_pixelFormat = GL_RGBA;
    int m_bytesPerPixel = 4;
    int m_captureSlotCount = 4;
    int m_nextSequenceToEncode = 0;
    bool m_active = false;
};

} // namespace CSlice

#endif // CSLICE_SLICECAPTUREOUTPUT_H
