/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sliceencoder.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <iomanip>
#include <sstream>

#ifdef _MSC_VER
#include <malloc.h>
#undef av_err2str
#define av_err2str(errnum) av_make_error_string(reinterpret_cast<char*>(_alloca(AV_ERROR_MAX_STRING_SIZE)), AV_ERROR_MAX_STRING_SIZE, errnum)
#endif

namespace {

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string normalizedCodecName(std::string value)
{
    value = toLower(std::move(value));
    std::replace(value.begin(), value.end(), '_', ' ');
    std::replace(value.begin(), value.end(), '-', ' ');
    return value;
}

std::string toString(int value)
{
    return std::to_string(value);
}

const char *codecName(const AVCodec *codec)
{
    return codec && codec->long_name ? codec->long_name : "unknown";
}

void setCodecOption(AVDictionary **options, const char *name, const std::string &value)
{
    if (options && name && !value.empty()) {
        av_dict_set(options, name, value.c_str(), 0);
    }
}

void setCodecOption(AVDictionary **options, const char *name, const char *value)
{
    if (options && name && value && value[0] != '\0') {
        av_dict_set(options, name, value, 0);
    }
}

std::string nvencPresetName(std::string preset)
{
    preset = toLower(std::move(preset));
    for (char character : preset) {
        if (character >= '1' && character <= '7') {
            return std::string("p") + character;
        }
    }
    return "p4";
}

std::string libxTuneName(std::string tune)
{
    tune = normalizedCodecName(std::move(tune));
    if (tune == "none" || tune == "default") return {};
    if (tune == "film") return "film";
    if (tune == "animation") return "animation";
    if (tune == "grain") return "grain";
    if (tune == "still image" || tune == "stillimage") return "stillimage";
    if (tune == "psnr") return "psnr";
    if (tune == "ssim") return "ssim";
    if (tune == "fast decode" || tune == "fastdecode") return "fastdecode";
    if (tune == "zero latency" || tune == "zerolatency") return "zerolatency";
    return tune;
}

std::string nvencTuneName(std::string tune)
{
    tune = normalizedCodecName(std::move(tune));
    if (tune == "none" || tune == "default") return {};
    if (tune == "high quality" || tune == "hq") return "hq";
    if (tune == "low latency" || tune == "ll") return "ll";
    if (tune == "ultra low latency" || tune == "ull") return "ull";
    if (tune == "lossless") return "lossless";
    return tune;
}

bool codecSupports10Bit(AVCodecID codecId)
{
    return codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC || codecId == AV_CODEC_ID_PRORES || codecId == AV_CODEC_ID_FFV1;
}

int bytesPerPixelForFormat(AVPixelFormat pixelFormat)
{
    switch (pixelFormat) {
        case AV_PIX_FMT_BGR24:
        case AV_PIX_FMT_RGB24:
            return 3;
        case AV_PIX_FMT_BGRA:
        case AV_PIX_FMT_RGBA:
        default:
            return 4;
    }
}

} // namespace

namespace CSlice {

SliceEncoder::SliceEncoder() = default;

SliceEncoder::~SliceEncoder()
{
    cleanup();
}

void SliceEncoder::setDesiredCodec(AVCodecID codec)
{
    m_desiredCodec = codec;
}

void SliceEncoder::setHardwareEncoderName(std::string encoderName)
{
    m_hardwareEncoderName = std::move(encoderName);
}

void SliceEncoder::setFrameRate(int numerator, int denominator)
{
    m_outputFrameRateNum = std::max(1, numerator);
    m_outputFrameRateDen = std::max(1, denominator);
}

void SliceEncoder::setInputFrameRate(int numerator, int denominator)
{
    m_inputFrameRateNum = std::max(1, numerator);
    m_inputFrameRateDen = std::max(1, denominator);
}

void SliceEncoder::setUseOnlyIframes(bool enabled)
{
    m_controllerUseOnlyIframes = enabled;
}

void SliceEncoder::setGopSize(int gopSize)
{
    m_controllerGopSize = std::clamp(gopSize, 1, 60);
}

void SliceEncoder::setConstantQuality(int constantQuality)
{
    if (constantQuality >= 0 && constantQuality <= 51) {
        m_constantQuality = constantQuality;
    }
}

void SliceEncoder::setPreset(std::string preset)
{
    m_preset = std::move(preset);
}

void SliceEncoder::setLibxTune(std::string tune)
{
    m_libxTune = std::move(tune);
}

void SliceEncoder::setNvencTune(std::string tune)
{
    m_nvencTune = std::move(tune);
}

void SliceEncoder::setNvencHardwareFrames(bool enabled)
{
    m_nvencHardwareFrames = enabled;
}

void SliceEncoder::setEncodingBitDepth(int bitDepth)
{
    m_encodingBitDepth = bitDepth >= 10 ? 10 : 8;
}

void SliceEncoder::setParameterFile(std::filesystem::path filePath)
{
    m_parameterFile = std::move(filePath);
}

void SliceEncoder::setInputPixelFormat(AVPixelFormat pixelFormat)
{
    m_inputPixelFormat = pixelFormat;
    m_inputBytesPerPixel = bytesPerPixelForFormat(pixelFormat);
}

bool SliceEncoder::init(const std::filesystem::path &filename, int width, int height, int pixrate, unsigned int maxEncoderThreads)
{
    cleanup();
    m_width = width;
    m_height = height;
    m_pixrate = std::max(1, pixrate);
    m_frameCounter = 0;

    const std::string filenameString = filename.string();
    std::error_code removeError;
    if (std::filesystem::exists(filename, removeError)) {
        std::filesystem::remove(filename, removeError);
        if (removeError || std::filesystem::exists(filename)) {
            std::fprintf(stderr, "Failed to remove existing output file '%s': %s\n", filenameString.c_str(), removeError.message().c_str());
            return false;
        }
    }

    int ret = avformat_alloc_output_context2(&m_formatContext, nullptr, nullptr, filenameString.c_str());
    if (ret < 0 || !m_formatContext) {
        std::fprintf(stderr, "Failed to allocate output context for '%s': %s\n", filenameString.c_str(), av_err2str(ret));
        return false;
    }

    const AVCodec *codec = nullptr;
    if (!m_hardwareEncoderName.empty()) {
        codec = avcodec_find_encoder_by_name(m_hardwareEncoderName.c_str());
    }
    else if (m_desiredCodec == AV_CODEC_ID_NONE) {
        codec = avcodec_find_encoder(m_formatContext->oformat->video_codec);
    }
    else if (m_desiredCodec == AV_CODEC_ID_VP8) {
        codec = avcodec_find_encoder_by_name("libvpx");
    }
    else if (m_desiredCodec == AV_CODEC_ID_VP9) {
        codec = avcodec_find_encoder_by_name("libvpx-vp9");
    }
    else if (m_desiredCodec == AV_CODEC_ID_PRORES) {
        codec = avcodec_find_encoder_by_name("prores_ks");
    }
    else if (m_desiredCodec == AV_CODEC_ID_H264) {
        // Force libx264; otherwise newer FFmpeg builds may pick a hardware encoder
        // (h264_nvenc/h264_qsv/h264_amf) for which the libx264-specific private
        // options applied below (crf/preset/profile/tune) are invalid and crash
        // inside avcodec_open2.
        codec = avcodec_find_encoder_by_name("libx264");
        if (!codec) {
            codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        }
    }
    else if (m_desiredCodec == AV_CODEC_ID_HEVC) {
        codec = avcodec_find_encoder_by_name("libx265");
        if (!codec) {
            codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
        }
    }
    else if (m_desiredCodec == AV_CODEC_ID_FFV1) {
        codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    }
    else {
        codec = avcodec_find_encoder(m_desiredCodec);
    }

    if (!codec) {
        std::fprintf(stderr, "Requested FFmpeg encoder was not found for '%s'\n", filenameString.c_str());
        return false;
    }

    // Follow FFmpeg 5.1 muxing.c example: create the stream first with NULL codec,
    // assign its id, then allocate the encoder context separately. Avoids
    // codec-defaults being applied twice through both avformat_new_stream and
    // avcodec_alloc_context3 (which can leave inconsistent fields and trip the
    // libx264 wrapper inside avcodec_open2).
    m_stream = avformat_new_stream(m_formatContext, nullptr);
    if (!m_stream) {
        std::fprintf(stderr, "Failed to create output video stream\n");
        return false;
    }
    m_stream->id = static_cast<int>(m_formatContext->nb_streams - 1);

    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) {
        std::fprintf(stderr, "Failed to allocate codec context for '%s'\n", codecName(codec));
        return false;
    }
    m_codecContext->codec_id = codec->id;
    m_codecContext->codec_type = AVMEDIA_TYPE_VIDEO;

    AVDictionary *codecOptions = nullptr;
    if (!setupCodec(maxEncoderThreads, &codecOptions)) {
        av_dict_free(&codecOptions);
        return false;
    }

    if (nvencHardwareFramesRequested() && !setupNvencHardwareFrames()) {
        std::fprintf(stderr, "Falling back to software-frame NVENC input.\n");
    }

    ret = avcodec_open2(m_codecContext, codec, &codecOptions);
    if (ret < 0) {
        av_dict_free(&codecOptions);
        std::fprintf(stderr, "Failed to open codec '%s': %s\n", codecName(codec), av_err2str(ret));
        return false;
    }
    av_dict_free(&codecOptions);

    const AVPixelFormat conversionPixelFormat = m_usingNvencHardwareFrames && m_encoderHardwareFramesContext
        ? reinterpret_cast<AVHWFramesContext *>(m_encoderHardwareFramesContext->data)->sw_format
        : m_codecContext->pix_fmt;

    m_swsContext = sws_getContext(m_width,
        m_height,
        m_inputPixelFormat,
        m_width,
        m_height,
        conversionPixelFormat,
        SWS_FAST_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (!m_swsContext) {
        std::fprintf(stderr, "Could not allocate frame conversion context\n");
        return false;
    }

    applyInputFrameRateFilter();

    if (!setupFilters()) {
        return false;
    }

    ret = avcodec_parameters_from_context(m_stream->codecpar, m_codecContext);
    if (ret < 0) {
        std::fprintf(stderr, "Could not copy codec parameters: %s\n", av_err2str(ret));
        return false;
    }

    if (!(m_formatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_formatContext->pb, filenameString.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::fprintf(stderr, "Could not open '%s': %s\n", filenameString.c_str(), av_err2str(ret));
            return false;
        }
    }

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "write_tmcd", "1", 0);
    ret = avformat_write_header(m_formatContext, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        std::fprintf(stderr, "Error opening output file '%s': %s\n", filenameString.c_str(), av_err2str(ret));
        return false;
    }

    if (!allocateBuffers()) {
        return false;
    }

    m_ok = true;
    startWorker();
    return true;
}

bool SliceEncoder::addRgbaPixels(const unsigned char *pixels)
{
    if (!pixels) {
        return false;
    }

    const size_t bytes = static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * static_cast<size_t>(m_inputBytesPerPixel);
    {
        std::unique_lock<std::mutex> lock(m_workerMutex);
        m_workerDone.wait(lock, [this]() {
            return m_workerFrameDone;
        });

        if (!m_ok || m_workerFatalError.load()) {
            m_ok = false;
            return false;
        }

        if (m_workerPixels.size() != bytes) {
            m_workerPixels.resize(bytes);
        }
        std::memcpy(m_workerPixels.data(), pixels, bytes);
        m_workerFrameDone = false;
    }
    m_workerStart.notify_one();
    return true;
}

bool SliceEncoder::encodeRgbaPixelsNow(const unsigned char *pixels)
{
    if (!m_ok || !pixels) {
        return false;
    }

    // Drop-frame logic: skip every 1001st frame for NTSC conversion (e.g., 30→29.97, 60→59.94)
    if (m_useDropFrame) {
        if (++m_dropFrameCounter % 1001 == 0) {
            // This frame is dropped - skip encoding it entirely
            return true;
        }
    }

    if (!convertPixelsToFrame(m_outputFrame, m_tmpFrame, pixels)) {
        m_ok = false;
        return false;
    }

    if (m_filterInitialized) {
        if (av_buffersrc_add_frame_flags(m_bufferSourceContext, m_outputFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            std::fprintf(stderr, "Error while feeding the filtergraph\n");
            m_ok = false;
            return false;
        }

        if (!drainFilterGraph()) {
            m_ok = false;
            return false;
        }
        return true;
    }

    m_ok = encodeFrame(m_outputFrame);
    return m_ok;
}

bool SliceEncoder::drainFilterGraph()
{
    while (true) {
        const int ret = av_buffersink_get_frame(m_bufferSinkContext, m_filteredFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            std::fprintf(stderr, "Error while reading from the filtergraph\n");
            return false;
        }
        if (!encodeFrame(m_filteredFrame)) {
            av_frame_unref(m_filteredFrame);
            return false;
        }
        av_frame_unref(m_filteredFrame);
    }
    return true;
}

bool SliceEncoder::flushFilterGraph()
{
    if (!m_filterInitialized) {
        return true;
    }

    if (av_buffersrc_add_frame_flags(m_bufferSourceContext, nullptr, 0) < 0) {
        std::fprintf(stderr, "Error while flushing the filtergraph\n");
        return false;
    }

    return drainFilterGraph();
}

bool SliceEncoder::isOk() const
{
    return m_ok;
}

bool SliceEncoder::usingNvencHardwareFrames() const
{
    return m_usingNvencHardwareFrames;
}

bool SliceEncoder::nvencHardwareFramesRequestedByUser() const
{
    return m_nvencHardwareFrames;
}

void SliceEncoder::finish()
{
    cleanup();
}

bool SliceEncoder::encodeStill(const std::filesystem::path &filename,
    AVCodecID codec,
    int width,
    int height,
    const unsigned char *bgrPixels,
    const std::filesystem::path &parameterFile)
{
    SliceEncoder encoder;
    encoder.setDesiredCodec(codec);
    encoder.setFrameRate(1, 1);
    encoder.setParameterFile(parameterFile);
    encoder.setInputPixelFormat(AV_PIX_FMT_BGR24);
    if (!encoder.init(filename, width, height, 6, 1)) {
        return false;
    }
    return encoder.encodeRgbaPixelsNow(bgrPixels);
}

bool SliceEncoder::convertPixelsToFrame(AVFrame *outFrame, AVFrame *tmpFrame, const unsigned char *pixels)
{
    int ret = av_frame_make_writable(outFrame);
    if (ret < 0) {
        std::fprintf(stderr, "Error making frame writable\n");
        return false;
    }

    ret = av_image_fill_arrays(tmpFrame->data, tmpFrame->linesize, pixels, m_inputPixelFormat, m_width, m_height, 1);
    if (ret < 0) {
        std::fprintf(stderr, "Error filling image arrays\n");
        return false;
    }

    tmpFrame->data[0] += tmpFrame->linesize[0] * (m_height - 1);
    tmpFrame->linesize[0] = -tmpFrame->linesize[0];

    ret = sws_scale(m_swsContext, tmpFrame->data, tmpFrame->linesize, 0, m_height, outFrame->data, outFrame->linesize);
    if (ret < 0) {
        std::fprintf(stderr, "Failed to convert frame %d\n", m_frameCounter);
        return false;
    }

    outFrame->pts = m_frameCounter++;
    return true;
}

bool SliceEncoder::encodeFrame(AVFrame *frame)
{
    AVFrame *frameToEncode = frame;
    AVFrame *hardwareFrame = nullptr;

    if (frame && m_usingNvencHardwareFrames) {
        hardwareFrame = av_frame_alloc();
        if (!hardwareFrame) {
            std::fprintf(stderr, "Could not allocate CUDA frame for NVENC upload\n");
            return false;
        }

        int uploadRet = av_hwframe_get_buffer(m_encoderHardwareFramesContext, hardwareFrame, 0);
        if (uploadRet < 0) {
            std::fprintf(stderr, "Could not allocate CUDA frame buffer for NVENC upload: %s\n", av_err2str(uploadRet));
            av_frame_free(&hardwareFrame);
            return false;
        }

        uploadRet = av_hwframe_transfer_data(hardwareFrame, frame, 0);
        if (uploadRet < 0) {
            std::fprintf(stderr, "Could not upload frame to CUDA for NVENC: %s\n", av_err2str(uploadRet));
            av_frame_free(&hardwareFrame);
            return false;
        }
        hardwareFrame->pts = frame->pts;
        frameToEncode = hardwareFrame;
    }

    int ret = avcodec_send_frame(m_codecContext, frameToEncode);
    if (hardwareFrame) {
        av_frame_free(&hardwareFrame);
    }
    if (ret < 0) {
        std::fprintf(stderr, "Error sending frame for encoding: %s\n", av_err2str(ret));
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(m_codecContext, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return true;
        }
        if (ret < 0) {
            std::fprintf(stderr, "Error during encoding: %s\n", av_err2str(ret));
            return false;
        }

        if (m_packet->duration <= 0) {
            m_packet->duration = 1;
        }
        av_packet_rescale_ts(m_packet, m_codecContext->time_base, m_stream->time_base);
        m_packet->stream_index = m_stream->index;
        ret = av_interleaved_write_frame(m_formatContext, m_packet);
        av_packet_unref(m_packet);
        if (ret < 0) {
            std::fprintf(stderr, "Error writing encoded packet: %s\n", av_err2str(ret));
            return false;
        }
    }

    return true;
}

bool SliceEncoder::allocateBuffers()
{
    m_packet = av_packet_alloc();
    if (!m_packet) {
        std::fprintf(stderr, "Could not allocate packet\n");
        return false;
    }

    m_outputFrame = av_frame_alloc();
    if (!m_outputFrame) {
        std::fprintf(stderr, "Could not allocate output frame\n");
        return false;
    }
    const AVPixelFormat outputPixelFormat = m_usingNvencHardwareFrames && m_encoderHardwareFramesContext
        ? reinterpret_cast<AVHWFramesContext *>(m_encoderHardwareFramesContext->data)->sw_format
        : m_codecContext->pix_fmt;
    m_outputFrame->format = outputPixelFormat;
    m_outputFrame->width = m_width;
    m_outputFrame->height = m_height;
    if (av_frame_get_buffer(m_outputFrame, 0) < 0) {
        std::fprintf(stderr, "Could not allocate output frame buffer\n");
        return false;
    }

    m_tmpFrame = av_frame_alloc();
    if (!m_tmpFrame) {
        std::fprintf(stderr, "Could not allocate temporary frame\n");
        return false;
    }
    m_tmpFrame->format = m_inputPixelFormat;
    m_tmpFrame->width = m_width;
    m_tmpFrame->height = m_height;

    m_filteredFrame = av_frame_alloc();
    if (!m_filteredFrame) {
        std::fprintf(stderr, "Could not allocate filter frame\n");
        return false;
    }

    return true;
}

bool SliceEncoder::setupCodec(unsigned int maxEncoderThreads, AVDictionary **codecOptions)
{
    unsigned int numberOfThreads = std::max(2u, std::thread::hardware_concurrency());
    numberOfThreads = std::min(numberOfThreads, std::max(1u, maxEncoderThreads));
    if (m_codecContext->codec_id == AV_CODEC_ID_HEVC) {
        numberOfThreads = std::min(numberOfThreads, 16u);
    }
    const char *encoderName = m_codecContext->codec && m_codecContext->codec->name ? m_codecContext->codec->name : "";
    const bool isNvenc = (std::strcmp(encoderName, "h264_nvenc") == 0) || (std::strcmp(encoderName, "hevc_nvenc") == 0);

    m_codecContext->width = m_width;
    m_codecContext->height = m_height;
    if (isNvenc) {
        std::fprintf(stderr, "NVENC selected; encoder thread count is handled by the hardware encoder.\n");
    }
    else {
        m_codecContext->thread_count = static_cast<int>(numberOfThreads);
        std::fprintf(stderr, "Encoder using %u thread(s)\n", numberOfThreads);
    }
    m_codecContext->time_base = AVRational{ m_outputFrameRateNum, m_outputFrameRateDen };
    m_codecContext->framerate = AVRational{ m_outputFrameRateDen, m_outputFrameRateNum };
    m_stream->time_base = AVRational{ m_outputFrameRateNum, m_outputFrameRateDen };

    // Pass input framerate to FFmpeg encoder for proper timing
    const std::string inputFpsString = toString(m_inputFrameRateNum) + "/" + toString(m_inputFrameRateDen);
    av_dict_set(codecOptions, "framerate", inputFpsString.c_str(), 0);
    m_codecContext->pix_fmt = m_encodingBitDepth >= 10 && codecSupports10Bit(m_codecContext->codec_id) ? AV_PIX_FMT_YUV420P10 : AV_PIX_FMT_YUV420P;
    m_codecContext->sample_aspect_ratio = AVRational{ 1, 1 };
    m_codecContext->max_b_frames = 0;
    m_codecContext->delay = 0;
    setCodecOption(codecOptions, "bf", "0");

    if (m_controllerUseOnlyIframes) {
        m_codecContext->gop_size = 1;
        setCodecOption(codecOptions, "strict-gop", "1");
        setCodecOption(codecOptions, "no-scenecut", "1");
    }
    else {
        m_codecContext->gop_size = std::clamp(m_controllerGopSize, 1, 60);
    }
    m_codecContext->trellis = 1;

    if (m_formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
        m_codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (m_constantQuality < 0) {
        const int rate = m_pixrate * m_width * m_height;
        m_codecContext->bit_rate = rate;
        m_codecContext->rc_min_rate = rate;
        m_codecContext->rc_max_rate = rate;
        m_codecContext->rc_buffer_size = 2 * rate;
        m_codecContext->rc_max_available_vbv_use = 0.8f;
    }
    else if (m_codecContext->codec_id != AV_CODEC_ID_H264 && m_codecContext->codec_id != AV_CODEC_ID_HEVC
             && m_codecContext->codec_id != AV_CODEC_ID_FFV1) {
        m_codecContext->flags |= AV_CODEC_FLAG_QSCALE;
        m_codecContext->global_quality = FF_QP2LAMBDA * m_constantQuality;
    }

    if (m_codecContext->codec_id == AV_CODEC_ID_H264 || m_codecContext->codec_id == AV_CODEC_ID_HEVC) {
        const bool isLibx = (std::strcmp(encoderName, "libx264") == 0) || (std::strcmp(encoderName, "libx265") == 0);
        if (isLibx && m_constantQuality >= 0) {
            setCodecOption(codecOptions, "crf", toString(m_constantQuality));
            setCodecOption(codecOptions, "tune", libxTuneName(m_libxTune));
        }
        if (isLibx && m_codecContext->codec_id == AV_CODEC_ID_H264) {
            setCodecOption(codecOptions, "profile", "high");
        }
        if (isLibx) {
            setCodecOption(codecOptions, "preset", m_preset);
        }
        if (isNvenc) {
            if (m_encodingBitDepth >= 10) {
                m_codecContext->pix_fmt = AV_PIX_FMT_P010;
            }
            setCodecOption(codecOptions, "preset", nvencPresetName(m_preset));
            setCodecOption(codecOptions, "rc", "constqp");
            if (m_constantQuality >= 0) {
                setCodecOption(codecOptions, "qp", toString(m_constantQuality));
            }
            setCodecOption(codecOptions, "tune", nvencTuneName(m_nvencTune));
            if (m_controllerUseOnlyIframes) {
                setCodecOption(codecOptions, "g", "0");
                setCodecOption(codecOptions, "rc-lookahead", "0");
                setCodecOption(codecOptions, "spatial-aq", "0");
                setCodecOption(codecOptions, "temporal-aq", "0");
            }
            setCodecOption(codecOptions, "bf", "0");
            setCodecOption(codecOptions, "b_ref_mode", "0");
        }
        if (std::strcmp(encoderName, "libx265") == 0) {
            if (m_controllerUseOnlyIframes) {
                setCodecOption(codecOptions, "x265-params", "keyint=1");
            }
            std::string x265Params = std::format("pools={}", numberOfThreads);
            setCodecOption(codecOptions, "x265-params", x265Params);
            std::fprintf(stderr, "x265 params: %s\n", x265Params.c_str());
        }
    }
    else if (m_codecContext->codec_id == AV_CODEC_ID_VP8) {
        if (m_constantQuality < 0) {
            setCodecOption(codecOptions, "quality", "realtime");
            setCodecOption(codecOptions, "bufsize", toString(m_codecContext->rc_buffer_size));
            setCodecOption(codecOptions, "vb", toString(static_cast<int>(m_codecContext->bit_rate)));
        }
        else {
            setCodecOption(codecOptions, "crf", toString(m_constantQuality));
        }
        setCodecOption(codecOptions, "threads", toString(static_cast<int>(numberOfThreads)));
        setCodecOption(codecOptions, "end-usage", "cbr");
        setCodecOption(codecOptions, "frame-parallel", "1");
        setCodecOption(codecOptions, "passes", "1");
        setCodecOption(codecOptions, "profile", "1");
        setCodecOption(codecOptions, "cpu-used", "0");
    }
    else if (m_codecContext->codec_id == AV_CODEC_ID_VP9) {
        if (m_constantQuality < 0) {
            setCodecOption(codecOptions, "quality", "realtime");
            setCodecOption(codecOptions, "bufsize", toString(m_codecContext->rc_buffer_size));
            setCodecOption(codecOptions, "vb", toString(static_cast<int>(m_codecContext->bit_rate)));
        }
        else {
            setCodecOption(codecOptions, "crf", toString(m_constantQuality));
            setCodecOption(codecOptions, "b:v", "0");
        }
        setCodecOption(codecOptions, "threads", toString(static_cast<int>(numberOfThreads)));
        setCodecOption(codecOptions, "speed", "2");
        setCodecOption(codecOptions, "frame-parallel", "1");
        setCodecOption(codecOptions, "tile-columns", toString(static_cast<int>(numberOfThreads / 2)));
        setCodecOption(codecOptions, "auto-alt-ref", "1");
        setCodecOption(codecOptions, "lag-in-frames", "25");
        setCodecOption(codecOptions, "cpu-used", "0");
    }
    else if (m_codecContext->codec_id == AV_CODEC_ID_PRORES) {
        m_codecContext->profile = 3;
        m_codecContext->pix_fmt = AV_PIX_FMT_YUV444P10;
    }
#ifdef AV_CODEC_ID_HAP
    else if (m_codecContext->codec_id == AV_CODEC_ID_HAP) {
        m_codecContext->pix_fmt = AV_PIX_FMT_RGBA;
    }
#endif
    else if (m_codecContext->codec_id == AV_CODEC_ID_PNG) {
        m_codecContext->pix_fmt = AV_PIX_FMT_RGBA;
    }
    else if (m_codecContext->codec_id == AV_CODEC_ID_MJPEG) {
        m_codecContext->pix_fmt = AV_PIX_FMT_YUVJ420P;
    }
    else if (m_codecContext->codec_id == AV_CODEC_ID_TARGA) {
        m_codecContext->pix_fmt = AV_PIX_FMT_BGRA;
    }
    else if (m_codecContext->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
        m_codecContext->mb_decision = 2;
    }
    else if (m_codecContext->codec_id == AV_CODEC_ID_FFV1) {
        // FFV1 configuration - lossless codec with configurable version level
        // level 3 is the default (newest features), can be overridden via Parameter JSON
        setCodecOption(codecOptions, "level", "3");
        setCodecOption(codecOptions, "threads", toString(static_cast<int>(numberOfThreads)));
    }

    setupOptionsFromJson(codecOptions);
    return true;
}

bool SliceEncoder::nvencHardwareFramesRequested() const
{
    if (!m_nvencHardwareFrames || !m_codecContext || !m_codecContext->codec) {
        return false;
    }

    const char *encoderName = m_codecContext->codec->name ? m_codecContext->codec->name : "";
    return std::strcmp(encoderName, "h264_nvenc") == 0 || std::strcmp(encoderName, "hevc_nvenc") == 0;
}

bool SliceEncoder::setupNvencHardwareFrames()
{
    if (!nvencHardwareFramesRequested()) {
        return false;
    }

    const AVPixelFormat softwareFormat = m_codecContext->pix_fmt;
    int ret = av_hwdevice_ctx_create(&m_encoderHardwareDeviceContext, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    if (ret < 0) {
        std::fprintf(stderr, "Could not create CUDA device for NVENC hardware frames: %s\n", av_err2str(ret));
        return false;
    }

    m_encoderHardwareFramesContext = av_hwframe_ctx_alloc(m_encoderHardwareDeviceContext);
    if (!m_encoderHardwareFramesContext) {
        std::fprintf(stderr, "Could not allocate CUDA frame context for NVENC hardware frames\n");
        av_buffer_unref(&m_encoderHardwareDeviceContext);
        return false;
    }

    AVHWFramesContext *framesContext = reinterpret_cast<AVHWFramesContext *>(m_encoderHardwareFramesContext->data);
    framesContext->format = AV_PIX_FMT_CUDA;
    framesContext->sw_format = softwareFormat;
    framesContext->width = m_width;
    framesContext->height = m_height;
    framesContext->initial_pool_size = 8;

    ret = av_hwframe_ctx_init(m_encoderHardwareFramesContext);
    if (ret < 0) {
        std::fprintf(stderr, "Could not initialize CUDA frame context for NVENC hardware frames: %s\n", av_err2str(ret));
        av_buffer_unref(&m_encoderHardwareFramesContext);
        av_buffer_unref(&m_encoderHardwareDeviceContext);
        return false;
    }

    m_codecContext->hw_frames_ctx = av_buffer_ref(m_encoderHardwareFramesContext);
    if (!m_codecContext->hw_frames_ctx) {
        std::fprintf(stderr, "Could not attach CUDA frame context to NVENC encoder\n");
        av_buffer_unref(&m_encoderHardwareFramesContext);
        av_buffer_unref(&m_encoderHardwareDeviceContext);
        return false;
    }

    m_codecContext->pix_fmt = AV_PIX_FMT_CUDA;
    m_usingNvencHardwareFrames = true;
    std::fprintf(stderr, "NVENC using CUDA hardware frames (%s)\n", av_get_pix_fmt_name(softwareFormat));
    return true;
}

void SliceEncoder::applyInputFrameRateFilter()
{
    if (m_inputFrameRateNum <= 0 || m_outputFrameRateNum <= 0) {
        return;
    }

    const double inputFps = static_cast<double>(m_inputFrameRateNum) / static_cast<double>(m_inputFrameRateDen);
    const double outputFps = static_cast<double>(m_outputFrameRateDen) / static_cast<double>(m_outputFrameRateNum);
    if (inputFps <= 0.0 || outputFps <= 0.0) {
        return;
    }

    if (std::abs(inputFps - outputFps) <= 0.001 * outputFps) {
        return;
    }

    // Check for NTSC drop-frame conversion: input/output ratio should be ~1000/1001
    // This covers: 30→29.97, 60→59.94, etc.
    const double ratio = outputFps / inputFps;
    const double targetRatio = 1000.0 / 1001.0;
    if (std::abs(ratio - targetRatio) < 0.001) {
        m_useDropFrame = true;
        m_dropFrameCounter = 0;
        std::fprintf(stderr,
            "Input frame rate %.6f will be converted to output %.6f using drop-frame (drop 1 in every 1001 frames)\n",
            inputFps,
            outputFps);
        return;
    }

    // Note: Automatic fps filter for non-NTSC frame rate differences has been removed.
    // NTSC drop-frame conversion (30→29.97, 60→59.94) is still handled above via m_useDropFrame.
    // Manual filters can still be specified via parameter files (videoFilters array).
}

bool SliceEncoder::setupFilters()
{
    if (m_filter.empty()) {
        return true;
    }
    char src_args[512];
    int ret = 0;
    const char* failedStep = nullptr;

    const AVFilter* bufferSource = avfilter_get_by_name("buffer");
    const AVFilter* bufferSink = avfilter_get_by_name("buffersink");

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    m_filterGraph = avfilter_graph_alloc();
    // Create the expected array containing your destination pixel format terminated by AV_PIX_FMT_NONE
    enum AVPixelFormat allowed_pix_fmts[] = { m_codecContext->pix_fmt, AV_PIX_FMT_NONE };

    // Convert the enum format (e.g., AV_PIX_FMT_YUV420P or AV_PIX_FMT_CUDA) safely to a string string
    const char* pix_fmt_name = av_get_pix_fmt_name(m_codecContext->pix_fmt);
    if (!pix_fmt_name) {
        ret = AVERROR(EINVAL);
        failedStep = "av_get_pix_fmt_name (invalid pixel format)";
        goto end;
    }

    if (!outputs || !inputs || !m_filterGraph) {
        ret = AVERROR(ENOMEM);
        failedStep = "allocation of filter graph/in-out";
        goto end;
    }

    // Multi-thread the filter graph (Crucial for software fallback steps)
    m_filterGraph->nb_threads = 0; // 0 auto-detects optimal CPU thread count

    if (m_inputFrameRateNum <= 0 || m_inputFrameRateDen <= 0) {
        std::fprintf(stderr,
            "Warning: invalid input frame rate %d/%d before filter setup; falling back to output %d/%d\n",
            m_inputFrameRateNum, m_inputFrameRateDen, m_outputFrameRateNum, m_outputFrameRateDen);
    }
    if (m_codecContext->sample_aspect_ratio.num <= 0 || m_codecContext->sample_aspect_ratio.den <= 0) {
        std::fprintf(stderr, "Warning: invalid sample_aspect_ratio before filter setup; falling back to 1/1\n");
        m_codecContext->sample_aspect_ratio = AVRational{ 1, 1 };
    }

    // Configure Buffer Source Args
    std::snprintf(src_args,
        sizeof(src_args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
        m_codecContext->width,
        m_codecContext->height,
        m_codecContext->pix_fmt,
        m_inputFrameRateNum,
        m_inputFrameRateDen,
        m_codecContext->sample_aspect_ratio.num,
        m_codecContext->sample_aspect_ratio.den);

    std::fprintf(stderr, "[setupFilters] buffersrc args: %s\n", src_args);
    std::fprintf(stderr, "[setupFilters] filter description: '%s'\n", m_filter.c_str());

    ret = avfilter_graph_create_filter(&m_bufferSourceContext, bufferSource, "in", src_args, nullptr, m_filterGraph);
    if (ret < 0) {
        failedStep = "avfilter_graph_create_filter(buffer source)";
        goto end;
    }

    // 4. Configure Buffer Sink using the modern string-based format option
    m_bufferSinkContext = avfilter_graph_alloc_filter(m_filterGraph, bufferSink, "out");
    if (!m_bufferSinkContext) {
        ret = AVERROR(ENOMEM);
        failedStep = "avfilter_graph_alloc_filter(buffer sink)";
        goto end;
    }

    // Modern FFmpeg handles format limitations via standard string parsing
    ret = av_opt_set(m_bufferSinkContext, "pixel_formats", pix_fmt_name, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        failedStep = "av_opt_set(pixel_formats)";
        goto end;
    }

    // Finalize initialization with options cleanly attached
    ret = avfilter_init_str(m_bufferSinkContext, nullptr);
    if (ret < 0) {
        failedStep = "avfilter_init_str(buffer sink)";
        goto end;
    }

    // Connect the Graph Structs
    outputs->name = av_strdup("in");
    outputs->filter_ctx = m_bufferSourceContext;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = m_bufferSinkContext;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    ret = avfilter_graph_parse_ptr(m_filterGraph, m_filter.c_str(), &inputs, &outputs, nullptr);
    if (ret < 0) {
        failedStep = "avfilter_graph_parse_ptr";
        goto end;
    }

    // Hardware device context initialization block
    if (m_hardwareFilterDeviceType != AV_HWDEVICE_TYPE_NONE) {
        ret = av_hwdevice_ctx_create(&m_hardwareFilterDeviceContext, m_hardwareFilterDeviceType, m_hardwareFilterDeviceId.c_str(), nullptr, 0);
        if (ret < 0) {
            failedStep = "av_hwdevice_ctx_create";
            goto end;
        }

        // FIX: hw_device_ctx is a property of the filters inside the graph, not the graph itself.
        for (unsigned int i = 0; i < m_filterGraph->nb_filters; ++i) {
            m_filterGraph->filters[i]->hw_device_ctx = av_buffer_ref(m_hardwareFilterDeviceContext);
            if (!m_filterGraph->filters[i]->hw_device_ctx) {
                ret = AVERROR(ENOMEM);
                failedStep = "av_buffer_ref(hw_device_ctx)";
                goto end;
            }
        }
    }

    ret = avfilter_graph_config(m_filterGraph, nullptr);
    if (ret < 0) {
        failedStep = "avfilter_graph_config";
    }

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0) {
        std::fprintf(stderr,
            "Failed to initialize video filter '%s' at step '%s': %s\n",
            m_filter.c_str(),
            failedStep ? failedStep : "unknown",
            av_err2str(ret));
        return false;
    }

    m_filterInitialized = true;
    return true;
}

void SliceEncoder::startWorker()
{
    stopWorker();
    m_workerFatalError = false;
    m_workerFrameDone = true;
    m_workerRunning = true;
    m_workerThread = std::thread(&SliceEncoder::workerLoop, this);
}

void SliceEncoder::stopWorker()
{
    if (!m_workerThread.joinable()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_workerRunning = false;
    }
    m_workerStart.notify_one();
    m_workerThread.join();

    if (m_workerFatalError.load()) {
        m_ok = false;
    }
}

void SliceEncoder::workerLoop()
{
    while (true) {
        std::unique_lock<std::mutex> lock(m_workerMutex);
        m_workerStart.wait(lock, [this]() {
            return !m_workerRunning || !m_workerFrameDone;
        });

        if (!m_workerRunning && m_workerFrameDone) {
            break;
        }

        const unsigned char *pixels = m_workerPixels.data();
        lock.unlock();
        const bool ok = encodeRgbaPixelsNow(pixels);
        lock.lock();

        if (!ok) {
            m_workerFatalError = true;
        }
        m_workerFrameDone = true;
        lock.unlock();
        m_workerDone.notify_all();

        if (!m_workerRunning) {
            break;
        }
    }
}

void SliceEncoder::setupOptionsFromJson(AVDictionary **codecOptions)
{
    if (m_parameterFile.empty()) {
        return;
    }

    QFile file(QString::fromStdWString(m_parameterFile.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "Could not open parameter file '%s'\n", m_parameterFile.string().c_str());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        std::fprintf(stderr, "Could not parse parameter file '%s': %s\n", m_parameterFile.string().c_str(), parseError.errorString().toUtf8().constData());
        return;
    }

    const QJsonObject root = document.object().value(QStringLiteral("codecParameters")).toObject();
    for (const QJsonValue &value : root.value(QStringLiteral("options")).toArray()) {
        const QJsonObject option = value.toObject();
        const QByteArray name = option.value(QStringLiteral("name")).toString().toUtf8();
        const QByteArray optionValue = option.value(QStringLiteral("value")).toString().toUtf8();
        if (!name.isEmpty() && !optionValue.isEmpty()) {
            std::fprintf(stderr, "AVOption -%s %s\n", name.constData(), optionValue.constData());
            av_dict_set(codecOptions, name.constData(), optionValue.constData(), 0);
        }
    }

    for (const QJsonValue &value : root.value(QStringLiteral("pixelFormats")).toArray()) {
        const QByteArray pixelFormatName = value.toString().toUtf8();
        const AVPixelFormat pixelFormat = av_get_pix_fmt(pixelFormatName.constData());
        if (pixelFormat != AV_PIX_FMT_NONE) {
            std::fprintf(stderr, "PixelFormat %s\n", pixelFormatName.constData());
            m_codecContext->pix_fmt = pixelFormat;
        }
    }

    for (const QJsonValue &value : root.value(QStringLiteral("videoFilters")).toArray()) {
        const QJsonObject filter = value.toObject();
        const QString description = filter.value(QStringLiteral("description")).toString();
        if (!description.isEmpty()) {
            m_filter = description.toStdString();
            std::fprintf(stderr, "VideoFilter Description: %s\n", m_filter.c_str());
        }
        const QString hardwareType = filter.value(QStringLiteral("hardwareType")).toString();
        if (!hardwareType.isEmpty()) {
            m_hardwareFilterDeviceType = av_hwdevice_find_type_by_name(hardwareType.toUtf8().constData());
            std::fprintf(stderr, "VideoFilter HW Type: %s\n", hardwareType.toUtf8().constData());
        }
        if (filter.contains(QStringLiteral("hardwareId"))) {
            m_hardwareFilterDeviceId = std::to_string(filter.value(QStringLiteral("hardwareId")).toInt());
            std::fprintf(stderr, "VideoFilter GPU ID: %s\n", m_hardwareFilterDeviceId.c_str());
        }
    }
}

void SliceEncoder::cleanup()
{
    stopWorker();

    if (m_ok) {
        flushFilterGraph();
        encodeFrame(nullptr);
        av_write_trailer(m_formatContext);
    }

    if (m_formatContext && !(m_formatContext->oformat->flags & AVFMT_NOFILE) && m_formatContext->pb) {
        avio_closep(&m_formatContext->pb);
    }
    if (m_filterGraph) {
        avfilter_graph_free(&m_filterGraph);
    }
    if (m_hardwareFilterDeviceContext) {
        av_buffer_unref(&m_hardwareFilterDeviceContext);
    }
    if (m_encoderHardwareFramesContext) {
        av_buffer_unref(&m_encoderHardwareFramesContext);
    }
    if (m_encoderHardwareDeviceContext) {
        av_buffer_unref(&m_encoderHardwareDeviceContext);
    }
    if (m_tmpFrame) {
        av_frame_free(&m_tmpFrame);
    }
    if (m_outputFrame) {
        av_frame_free(&m_outputFrame);
    }
    if (m_filteredFrame) {
        av_frame_free(&m_filteredFrame);
    }
    if (m_packet) {
        av_packet_free(&m_packet);
    }
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }
    if (m_formatContext) {
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
    }

    m_stream = nullptr;
    m_bufferSinkContext = nullptr;
    m_bufferSourceContext = nullptr;
    m_ok = false;
    m_filterInitialized = false;
    m_usingNvencHardwareFrames = false;
    m_workerPixels.clear();
}

AVCodecID codecIdFromName(std::string codecName)
{
    codecName = normalizedCodecName(std::move(codecName));
    if (codecName == "default" || codecName == "h264") return AV_CODEC_ID_H264;
    if (codecName == "mpeg-1") return AV_CODEC_ID_MPEG1VIDEO;
    if (codecName == "mpeg-2") return AV_CODEC_ID_MPEG2VIDEO;
    if (codecName == "mpeg-4") return AV_CODEC_ID_MPEG4;
    if (codecName == "h265" || codecName == "hevc" || codecName == "h265 nvenc" || codecName == "hevc nvenc") return AV_CODEC_ID_HEVC;
    if (codecName == "h264 nvenc") return AV_CODEC_ID_H264;
    if (codecName == "vp8") return AV_CODEC_ID_VP8;
    if (codecName == "vp9") return AV_CODEC_ID_VP9;
#ifdef AV_CODEC_ID_HAP
    if (codecName == "hap") return AV_CODEC_ID_HAP;
#endif
    if (codecName == "prores") return AV_CODEC_ID_PRORES;
    if (codecName == "ffv1") return AV_CODEC_ID_FFV1;
    if (codecName == "png") return AV_CODEC_ID_PNG;
    if (codecName == "jpeg" || codecName == "jpg") return AV_CODEC_ID_MJPEG;
    if (codecName == "tga") return AV_CODEC_ID_TARGA;
    return AV_CODEC_ID_H264;
}

bool isMovieCodec(AVCodecID codec)
{
    return codec != AV_CODEC_ID_PNG && codec != AV_CODEC_ID_MJPEG && codec != AV_CODEC_ID_TARGA;
}

std::filesystem::path numberedOutputPath(const std::filesystem::path &output, int frameIndex)
{
    std::ostringstream name;
    name << output.stem().string() << '_' << std::setw(6) << std::setfill('0') << frameIndex << output.extension().string();
    return output.parent_path() / name.str();
}

} // namespace CSlice
