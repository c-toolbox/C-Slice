/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_FFMPEGPROBE_H
#define CSLICE_FFMPEGPROBE_H

#include <QString>

#include <filesystem>

namespace CSlice::FFmpegProbe {

QString versionString();
QString libraryString();

// Returns an accurate number of video frames for the file, or 0 if it could
// not be determined. Prefers the container's reported frame count and falls
// back to demuxing and counting packets when necessary.
int accurateFrameCount(const std::filesystem::path &path);

} // namespace CSlice::FFmpegProbe

#endif // CSLICE_FFMPEGPROBE_H
