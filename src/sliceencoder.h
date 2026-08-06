/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SLICEENCODER_H
#define CSLICE_SLICEENCODER_H

extern "C" {
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace CSlice {

class SliceEncoder {
public:
    SliceEncoder();
    SliceEncoder(const SliceEncoder&) = delete;
    SliceEncoder& operator=(const SliceEncoder&) = delete;
    ~SliceEncoder();

    void setDesiredCodec(AVCodecID codec);
    void setHardwareEncoderName(std::string encoderName);
    void setFrameRate(int numerator, int denominator);
    void setInputFrameRate(int numerator, int denominator);
    void setUseOnlyIframes(bool enabled);
    void setGopSize(int gopSize);
    void setConstantQuality(int constantQuality);
    void setPreset(std::string preset);
    void setLibxTune(std::string tune);
    void setNvencTune(std::string tune);
    void setNvencHardwareFrames(bool enabled);
    void setEncodingBitDepth(int bitDepth);
    void setParameterFile(std::filesystem::path filePath);
    void setInputPixelFormat(AVPixelFormat pixelFormat);

    bool init(const std::filesystem::path &filename, int width, int height, int pixrate, unsigned int maxEncoderThreads);
    bool addRgbaPixels(const unsigned char *pixels);
    bool isOk() const;
    bool usingNvencHardwareFrames() const;
    bool nvencHardwareFramesRequestedByUser() const;
    void finish();

    static bool encodeStill(const std::filesystem::path &filename,
        AVCodecID codec,
        int width,
        int height,
        const unsigned char *bgrPixels,
        const std::filesystem::path &parameterFile = {});

private:
    bool encodeRgbaPixelsNow(const unsigned char *pixels);
    bool drainFilterGraph();
    bool flushFilterGraph();
    bool convertPixelsToFrame(AVFrame *outFrame, AVFrame *tmpFrame, const unsigned char *pixels);
    bool encodeFrame(AVFrame *frame);
    bool allocateBuffers();
    bool setupCodec(unsigned int maxEncoderThreads, AVDictionary **codecOptions);
    bool setupNvencHardwareFrames();
    bool nvencHardwareFramesRequested() const;
    bool setupFilters();
    void applyInputFrameRateFilter();
    void setupOptionsFromJson(AVDictionary **codecOptions);
    void startWorker();
    void stopWorker();
    void workerLoop();
    void cleanup();

    AVFormatContext *m_formatContext = nullptr;
    AVStream *m_stream = nullptr;
    AVCodecContext *m_codecContext = nullptr;
    SwsContext *m_swsContext = nullptr;
    AVFrame *m_tmpFrame = nullptr;
    AVFrame *m_filteredFrame = nullptr;
    AVFrame *m_outputFrame = nullptr;
    AVPacket *m_packet = nullptr;
    AVFilterContext *m_bufferSinkContext = nullptr;
    AVFilterContext *m_bufferSourceContext = nullptr;
    AVFilterGraph *m_filterGraph = nullptr;
    AVBufferRef *m_hardwareFilterDeviceContext = nullptr;
    AVHWDeviceType m_hardwareFilterDeviceType = AV_HWDEVICE_TYPE_NONE;
    AVBufferRef *m_encoderHardwareDeviceContext = nullptr;
    AVBufferRef *m_encoderHardwareFramesContext = nullptr;

    AVCodecID m_desiredCodec = AV_CODEC_ID_NONE;
    std::string m_hardwareEncoderName;
    std::filesystem::path m_parameterFile;
    std::string m_filter;
    std::string m_hardwareFilterDeviceId = "0";
    std::string m_preset = "fast";
    std::string m_libxTune = "fastdecode";
    std::string m_nvencTune = "hq";
    bool m_nvencHardwareFrames = false;
    bool m_usingNvencHardwareFrames = false;
    int m_frameCounter = 0;
    int m_width = 0;
    int m_height = 0;
    int m_pixrate = 6;
    int m_outputFrameRateNum = 1;
    int m_outputFrameRateDen = 30;
    int m_inputFrameRateNum = 30;
    int m_inputFrameRateDen = 1;
    bool m_controllerUseOnlyIframes = false;
    int m_controllerGopSize = 1;
    int m_constantQuality = -1;
    int m_encodingBitDepth = 8;
    AVPixelFormat m_inputPixelFormat = AV_PIX_FMT_RGBA;
    int m_inputBytesPerPixel = 4;
    bool m_ok = false;
    bool m_filterInitialized = false;
    bool m_useDropFrame = false;
    int m_dropFrameCounter = 0;

    std::thread m_workerThread;
    std::mutex m_workerMutex;
    std::condition_variable m_workerStart;
    std::condition_variable m_workerDone;
    std::vector<unsigned char> m_workerPixels;
    bool m_workerRunning = false;
    bool m_workerFrameDone = true;
    std::atomic_bool m_workerFatalError{false};
};

AVCodecID codecIdFromName(std::string codecName);
bool isMovieCodec(AVCodecID codec);
std::filesystem::path numberedOutputPath(const std::filesystem::path &output, int frameIndex);

} // namespace CSlice

#endif // CSLICE_SLICEENCODER_H
