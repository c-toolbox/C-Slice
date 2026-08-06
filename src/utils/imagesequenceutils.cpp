/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sunden <eriksunden85@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "imagesequenceutils.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <limits>

namespace {

bool isPreferredDuplicate(const ImageSequenceFrameInfo &candidate,
                          const ImageSequenceFrameInfo &existing,
                          const QString &selectedPath,
                          int selectedDigitCount)
{
    const QFileInfo candidateInfo(candidate.path);
    const QFileInfo existingInfo(existing.path);
    const bool candidateIsSelected = candidateInfo == QFileInfo(selectedPath);
    const bool existingIsSelected = existingInfo == QFileInfo(selectedPath);
    if (candidateIsSelected != existingIsSelected) {
        return candidateIsSelected;
    }

    const bool candidateMatchesDigitCount = candidate.digitCount == selectedDigitCount;
    const bool existingMatchesDigitCount = existing.digitCount == selectedDigitCount;
    if (candidateMatchesDigitCount != existingMatchesDigitCount) {
        return candidateMatchesDigitCount;
    }

    if (candidate.digitCount != existing.digitCount) {
        return candidate.digitCount > existing.digitCount;
    }

    return candidate.path < existing.path;
}

}

int ImageSequenceUtils::trailingDigitCount(const QString &text)
{
    int count = 0;
    for (int i = text.size() - 1; i >= 0; --i) {
        if (!text.at(i).isDigit()) {
            break;
        }
        ++count;
    }
    return count;
}

bool ImageSequenceUtils::containsOnlyDigits(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    for (const QChar c : text) {
        if (!c.isDigit()) {
            return false;
        }
    }
    return true;
}

ImageSequenceScanResult ImageSequenceUtils::parseImageSequencePattern(const QString &path, bool requireExistingFile)
{
    ImageSequenceScanResult result;
    const QFileInfo selected(path);
    if (requireExistingFile && (!selected.exists() || !selected.isFile())) {
        result.message = QStringLiteral("Input file does not exist: %1").arg(path);
        return result;
    }

    const QString selectedStem = selected.completeBaseName();
    const int digitCount = trailingDigitCount(selectedStem);
    if (digitCount == 0) {
        result.message = QStringLiteral("Selected input is not a numbered image-sequence frame: %1").arg(selected.fileName());
        result.count = 1;
        return result;
    }

    const QString prefix = selectedStem.left(selectedStem.size() - digitCount);
    const QString selectedDigits = selectedStem.right(digitCount);
    bool selectedOk = false;
    result.selectedIndex = selectedDigits.toInt(&selectedOk);
    if (!selectedOk) {
        result.message = QStringLiteral("Failed to parse frame number from selected input: %1").arg(selected.fileName());
        return result;
    }

    const QString suffix = selected.suffix();
    result.ok = true;
    result.firstIndex = result.selectedIndex;
    result.lastIndex = result.selectedIndex;
    result.count = 1;
    result.prefix = prefix;
    result.suffix = suffix;
    result.digitCount = digitCount;
    return result;
}

ImageSequenceScanResult ImageSequenceUtils::scanImageSequence(const QString &path, ScanProgressCallback progressCallback)
{
    ImageSequenceScanResult result = parseImageSequencePattern(path);
    if (!result.ok) {
        return result;
    }

    result.ok = false;
    result.count = 0;

    const std::vector<ImageSequenceFrameInfo> frames = collectImageSequenceFrames(path, std::move(progressCallback));
    if (frames.empty()) {
        const QFileInfo selected(path);
        result.message = QStringLiteral("No matching numbered image sequence files found for: %1").arg(selected.fileName());
        return result;
    }

    result.count = static_cast<int>(frames.size());

    result.ok = true;
    result.firstIndex = frames.front().frameIndex;
    result.lastIndex = frames.back().frameIndex;

    // Build a set of found frame indices for gap detection
    QSet<int> foundFrames;
    foundFrames.reserve(static_cast<int>(frames.size()));
    for (const auto &frame : frames) {
        foundFrames.insert(frame.frameIndex);
    }

    // Identify missing frame indices in the range [firstIndex, lastIndex]
    QList<int> missingIndices;
    const int expectedCount = 1 + result.lastIndex - result.firstIndex;
    if (expectedCount != result.count) {
        result.missingFrames = true;
        for (int i = result.firstIndex; i <= result.lastIndex; ++i) {
            if (!foundFrames.contains(i)) {
                missingIndices.append(i);
            }
        }
    }
    else {
        result.missingFrames = false;
    }
    result.missingFrameIndices = std::move(missingIndices);

    return result;
}

std::vector<ImageSequenceFrameInfo> ImageSequenceUtils::collectImageSequenceFrames(const QString &path,
                                                                                   ScanProgressCallback progressCallback)
{
    const ImageSequenceScanResult pattern = parseImageSequencePattern(path);
    if (!pattern.ok) {
        return {};
    }

    const QFileInfo selected(path);
    const QString suffix = pattern.suffix;
    const QStringList filters = suffix.isEmpty()
        ? QStringList{ QStringLiteral("*") }
        : QStringList{ QStringLiteral("*.%1").arg(suffix) };
    const QFileInfoList entries = QDir(selected.absolutePath()).entryInfoList(filters, QDir::Files, QDir::Name);

    std::vector<ImageSequenceFrameInfo> frames;
    frames.reserve(static_cast<std::size_t>(entries.size()));
    int indexedFiles = 0;
    for (const QFileInfo &entry : entries) {
        if (entry.suffix().compare(suffix, Qt::CaseInsensitive) != 0) {
            continue;
        }

        const QString stem = entry.completeBaseName();
        if (!stem.startsWith(pattern.prefix)) {
            continue;
        }

        const QString digits = stem.mid(pattern.prefix.size());
        if (digits.isEmpty() || !containsOnlyDigits(digits)) {
            continue;
        }

        bool ok = false;
        const int frameIndex = digits.toInt(&ok);
        if (!ok) {
            continue;
        }

        ImageSequenceFrameInfo candidate;
        candidate.frameIndex = frameIndex;
        candidate.digitCount = digits.size();
        candidate.path = entry.absoluteFilePath();

        auto existing = std::ranges::find(frames, frameIndex, &ImageSequenceFrameInfo::frameIndex);
        if (existing == frames.end()) {
            frames.push_back(std::move(candidate));
        }
        else if (isPreferredDuplicate(candidate, *existing, selected.absoluteFilePath(), pattern.digitCount)) {
            *existing = std::move(candidate);
        }

        ++indexedFiles;
        if (progressCallback && indexedFiles % 250 == 0 && !progressCallback(indexedFiles)) {
            return {};
        }
    }

    std::ranges::sort(frames, [](const ImageSequenceFrameInfo &left, const ImageSequenceFrameInfo &right) {
        if (left.frameIndex != right.frameIndex) {
            return left.frameIndex < right.frameIndex;
        }
        if (left.digitCount != right.digitCount) {
            return left.digitCount > right.digitCount;
        }
        return left.path < right.path;
    });

    if (progressCallback) {
        progressCallback(indexedFiles);
    }

    return frames;
}

QString ImageSequenceUtils::buildFramePath(const QString &directory, const QString &prefix,
                                           int digitCount, const QString &suffix, int frameIndex)
{
    const QString digits = QStringLiteral("%1").arg(frameIndex, digitCount, 10, QLatin1Char('0'));
    QString filename = prefix + digits;
    if (!suffix.isEmpty()) {
        filename += QLatin1Char('.') + suffix;
    }
    return QDir(directory).filePath(filename);
}

int ImageSequenceUtils::expectedFrameCount(int startIndex, int stopIndex, int steps)
{
    if (startIndex == stopIndex) {
        return 1;
    }
    const int step = std::max(1, steps);
    const int first = std::min(startIndex, stopIndex);
    const int last = std::max(startIndex, stopIndex);
    return ((last - first) / step) + 1;
}
