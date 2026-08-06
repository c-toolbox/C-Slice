/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sunden <eriksunden85@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IMAGESEQUENCEUTILS_H
#define IMAGESEQUENCEUTILS_H

#include <functional>
#include <vector>

#include <QList>
#include <QString>

struct ImageSequenceScanResult {
    bool ok = false;
    bool missingFrames = false;
    QList<int> missingFrameIndices;
    int count = 0;
    int firstIndex = 0;
    int selectedIndex = 0;
    int lastIndex = 0;
    QString prefix;
    QString suffix;
    int digitCount = 0;
    QString message;
};

struct ImageSequenceFrameInfo {
    int frameIndex = 0;
    int digitCount = 0;
    QString path;
};

class ImageSequenceUtils {
public:
    using ScanProgressCallback = std::function<bool(int indexedFiles)>;

    /**
     * Scan the directory of the given file path for numbered image sequence frames.
     * Returns information about the sequence (first/last index, count, etc.).
     */
    static ImageSequenceScanResult scanImageSequence(const QString &path, ScanProgressCallback progressCallback = {});

    /**
     * Collect numbered image-sequence files matching the selected file's prefix/suffix.
     * Files are returned in numeric frame order and support mixed digit padding.
     */
    static std::vector<ImageSequenceFrameInfo> collectImageSequenceFrames(const QString &path,
                                                                          ScanProgressCallback progressCallback = {});

    /**
     * Parse sequence metadata from the selected file path without scanning the directory.
     */
    static ImageSequenceScanResult parseImageSequencePattern(const QString &path, bool requireExistingFile = true);

    /**
     * Build a file path for a specific frame index given the sequence parameters.
     * E.g. prefix="img_", digitCount=4, suffix="png", frameIndex=42 -> "img_0042.png"
     */
    static QString buildFramePath(const QString &directory, const QString &prefix,
                                  int digitCount, const QString &suffix, int frameIndex);

    /**
     * Count the number of trailing digits in the given text.
     */
    static int trailingDigitCount(const QString &text);

    /**
     * Check if the text contains only digit characters.
     */
    static bool containsOnlyDigits(const QString &text);

    /**
     * Calculate the expected frame count for a sequence with given parameters.
     */
    static int expectedFrameCount(int startIndex, int stopIndex, int steps);
};

#endif // IMAGESEQUENCEUTILS_H
