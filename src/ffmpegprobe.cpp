/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ffmpegprobe.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <string>

namespace {

QString versionToString(unsigned version)
{
    return QStringLiteral("%1.%2.%3")
        .arg(AV_VERSION_MAJOR(version))
        .arg(AV_VERSION_MINOR(version))
        .arg(AV_VERSION_MICRO(version));
}

} // namespace

QString CSlice::FFmpegProbe::versionString()
{
    return QString::fromLatin1(av_version_info());
}

QString CSlice::FFmpegProbe::libraryString()
{
    return QStringLiteral("libavcodec %1, libavformat %2, libavutil %3, libavfilter %4, libswscale %5")
        .arg(versionToString(avcodec_version()))
        .arg(versionToString(avformat_version()))
        .arg(versionToString(avutil_version()))
        .arg(versionToString(avfilter_version()))
        .arg(versionToString(swscale_version()));
}

int CSlice::FFmpegProbe::accurateFrameCount(const std::filesystem::path &path)
{
    const std::u8string utf8 = path.u8string();
    if (utf8.empty()) {
        return 0;
    }
    const std::string filename(reinterpret_cast<const char *>(utf8.data()), utf8.size());

    AVFormatContext *formatContext = nullptr;
    if (avformat_open_input(&formatContext, filename.c_str(), nullptr, nullptr) < 0) {
        return 0;
    }

    int frameCount = 0;
    if (avformat_find_stream_info(formatContext, nullptr) >= 0) {
        const int streamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (streamIndex >= 0) {
            const AVStream *stream = formatContext->streams[streamIndex];

            // Prefer the container's reported frame count when available.
            if (stream->nb_frames > 0) {
                frameCount = static_cast<int>(stream->nb_frames);
            }
            else {
                // Demux and count the actual video packets.
                AVPacket *packet = av_packet_alloc();
                if (packet) {
                    long long counted = 0;
                    while (av_read_frame(formatContext, packet) >= 0) {
                        if (packet->stream_index == streamIndex) {
                            ++counted;
                        }
                        av_packet_unref(packet);
                    }
                    av_packet_free(&packet);
                    frameCount = static_cast<int>(counted);
                }
            }
        }
    }

    avformat_close_input(&formatContext);
    return std::max(0, frameCount);
}

