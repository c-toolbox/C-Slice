/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "slicecontroller.h"

#include "slicesettings.h"
#include "slicevideoloader.h"
#include "utils/imagesequenceutils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QWaitCondition>
#include <QMetaType>
#include <QRandomGenerator>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(cjobClient, "CSlice.cjobclient")

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>

namespace {

QString normalizedPath(QString path)
{
    path = path.trimmed();
    if (path.startsWith(QStringLiteral("file:///"))) {
        path = QUrl(path).toLocalFile();
    }
    return QDir::toNativeSeparators(path);
}

std::filesystem::path filesystemPathFromQString(const QString &path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

QString defaultSliceDataPath(const QString &relativePath)
{
    const QString dataPath = QStringLiteral("data/") + relativePath;
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList searchRoots = {
        applicationDir,
        QDir(applicationDir).filePath(QStringLiteral("slice")),
        QDir(applicationDir).filePath(QStringLiteral("..")),
        QDir(applicationDir).filePath(QStringLiteral("../..")),
        QDir(applicationDir).filePath(QStringLiteral("../../..")),
        QDir::currentPath(),
    };

    for (const QString &root : searchRoots) {
        const QString candidate = QDir::cleanPath(QDir(root).filePath(dataPath));
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }

    return QDir::toNativeSeparators(dataPath);
}

QString configuredSlicePath(QString path, const QString &fallbackRelativePath)
{
    path = normalizedPath(path);
    if (path.isEmpty()) {
        return defaultSliceDataPath(fallbackRelativePath);
    }

    const QFileInfo directInfo(path);
    if (directInfo.isAbsolute() || directInfo.exists()) {
        return path;
    }

    QString relativePath = QDir::fromNativeSeparators(path);
    if (relativePath.startsWith(QStringLiteral("data/"))) {
        relativePath.remove(0, QStringLiteral("data/").size());
    }
    if (relativePath.startsWith(QStringLiteral("configs/")) || relativePath.startsWith(QStringLiteral("parameters/"))) {
        return defaultSliceDataPath(relativePath);
    }

    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList searchRoots = {
        applicationDir,
        QDir(applicationDir).filePath(QStringLiteral("..")),
        QDir(applicationDir).filePath(QStringLiteral("../..")),
        QDir::currentPath(),
    };
    for (const QString &root : searchRoots) {
        const QString candidate = QDir::cleanPath(QDir(root).filePath(path));
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }

    return path;
}

QString normalizedMappingMode(QString mode)
{
    mode = mode.trimmed();
    if (mode.compare(QStringLiteral("Sphere EAC"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("EAC"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Sphere EAC");
    }
    if (mode.compare(QStringLiteral("Sphere EQR"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("Spherical 360"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("Spherical"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Sphere EQR");
    }
    if (mode.compare(QStringLiteral("Plane wide"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("Plane narrow"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("Plane"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Plane");
    }
    return QStringLiteral("Dome");
}

QString normalizedLayerStereoMode(QString mode)
{
    mode = mode.trimmed();
    if (mode.compare(QStringLiteral("3D (side-by-side)"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("side-by-side"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("sbs"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("3D (side-by-side)");
    }
    if (mode.compare(QStringLiteral("3D (top-bottom)"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("top-bottom"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("tb"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("3D (top-bottom)");
    }
    if (mode.compare(QStringLiteral("3D (top-bottom+flip)"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("top-bottom+flip"), Qt::CaseInsensitive) == 0 ||
        mode.compare(QStringLiteral("tb-flip"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("3D (top-bottom+flip)");
    }
    return QStringLiteral("2D (mono)");
}

QString normalizedInputType(QString inputType)
{
    inputType = inputType.trimmed();
    if (inputType.compare(QStringLiteral("video"), Qt::CaseInsensitive) == 0 ||
        inputType.compare(QStringLiteral("Video"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Video");
    }
    return QStringLiteral("Image sequence");
}

QString nodeInputTypeArgument(const QString &inputType)
{
    return normalizedInputType(inputType) == QStringLiteral("Video") ? QStringLiteral("video") : QStringLiteral("image");
}

double clampedRoiCoordinate(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double clampedRoiSize(double value)
{
    return std::clamp(value, 0.001, 1.0);
}

QString normalizedCodec(QString codec)
{
    codec = codec.trimmed().toLower();
    codec.replace(QLatin1Char('_'), QLatin1Char(' '));
    codec.replace(QLatin1Char('-'), QLatin1Char(' '));
    return codec;
}

bool isNvencCodecName(const QString &codec)
{
    const QString normalized = normalizedCodec(codec);
    return normalized == QStringLiteral("h264 nvenc") || normalized == QStringLiteral("h265 nvenc") || normalized == QStringLiteral("hevc nvenc");
}

bool isCrfCodecName(const QString &codec)
{
    const QString normalized = normalizedCodec(codec);
    return normalized == QStringLiteral("h264") || normalized == QStringLiteral("h265") || normalized == QStringLiteral("hevc");
}

bool isMovieCodecName(const QString &codec)
{
    const QString normalized = normalizedCodec(codec);
    return normalized != QStringLiteral("png") && normalized != QStringLiteral("jpeg") && normalized != QStringLiteral("tga");
}

bool hasQualityCodecName(const QString &codec)
{
    return normalizedCodec(codec) != QStringLiteral("hap");
}

bool hasBitrateCodecName(const QString &codec)
{
    return isMovieCodecName(codec) && !hasQualityCodecName(codec);
}

bool hasPresetCodecName(const QString &codec)
{
    return isCrfCodecName(codec) || isNvencCodecName(codec);
}

bool supportsEncodingBitDepthCodecName(const QString &codec)
{
    const QString normalized = normalizedCodec(codec);
    return isCrfCodecName(codec) || isNvencCodecName(codec) || normalized == QStringLiteral("prores");
}

QStringList outputContainerSuffixesForCodec(const QString &codec)
{
    const QString normalized = normalizedCodec(codec);
    if (normalized == QStringLiteral("mpeg 1")) return { QStringLiteral(".mpg") };
    if (normalized == QStringLiteral("mpeg 2")) return { QStringLiteral(".mp2") };
    if (normalized == QStringLiteral("mpeg 4") ||
        normalized == QStringLiteral("h264") ||
        normalized == QStringLiteral("h264 nvenc") ||
        normalized == QStringLiteral("h265") ||
        normalized == QStringLiteral("h265 nvenc") ||
        normalized == QStringLiteral("vp8") ||
        normalized == QStringLiteral("vp9")) {
        const QString defaultSuffix = (normalized == QStringLiteral("vp8") || normalized == QStringLiteral("vp9")) ? QStringLiteral(".webm") : QStringLiteral(".mp4");
        return { defaultSuffix, QStringLiteral(".mkv") };
    }
    if (normalized == QStringLiteral("hap") || normalized == QStringLiteral("prores")) return { QStringLiteral(".mov") };
    if (normalized == QStringLiteral("ffv1")) return { QStringLiteral(".mkv") };
    if (normalized == QStringLiteral("png")) return { QStringLiteral(".png") };
    if (normalized == QStringLiteral("jpeg") || normalized == QStringLiteral("jpg")) return { QStringLiteral(".jpg") };
    if (normalized == QStringLiteral("tga")) return { QStringLiteral(".tga") };
    return { QStringLiteral(".mp4") };
}

QString absolutePathFromWorkingDirectory(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return QString();
    }
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (QDir::isAbsolutePath(cleanPath)) {
        return cleanPath;
    }
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(cleanPath));
}

QString relativePathIfInWorkingDirectory(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return QString();
    }
    const QString absolutePath = absolutePathFromWorkingDirectory(path);
    const QDir workingDirectory(QCoreApplication::applicationDirPath());
    const QString relativePath = QDir::fromNativeSeparators(workingDirectory.relativeFilePath(absolutePath));
    if (relativePath != QStringLiteral(".") &&
        relativePath != QStringLiteral("..") &&
        !relativePath.startsWith(QStringLiteral("../")) &&
        !QDir::isAbsolutePath(relativePath)) {
        return QDir::toNativeSeparators(relativePath);
    }
    return QDir::toNativeSeparators(absolutePath);
}

QString cleanNativePath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return QString();
    }
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

QString ffmpegExecutable()
{
#ifdef Q_OS_WIN
    const QString executable = QStringLiteral("ffmpeg.exe");
#else
    const QString executable = QStringLiteral("ffmpeg");
#endif
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList searchPaths = {
        applicationDir,
        QDir(applicationDir).filePath(QStringLiteral("bin")),
        QDir(applicationDir).filePath(QStringLiteral("..")),
        QDir::currentPath(),
    };

    const QString localExecutable = QStandardPaths::findExecutable(executable, searchPaths);
    if (!localExecutable.isEmpty()) {
        return localExecutable;
    }

    const QString pathExecutable = QStandardPaths::findExecutable(executable);
    return pathExecutable.isEmpty() ? executable : pathExecutable;
}

QString wavOutputPath(QString outputFile)
{
    outputFile = normalizedPath(outputFile);
    if (outputFile.isEmpty()) {
        return QString();
    }
    if (!outputFile.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
        outputFile += QStringLiteral(".wav");
    }
    return cleanNativePath(absolutePathFromWorkingDirectory(outputFile));
}

QString standardPicturesPath()
{
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return pictures.isEmpty() ? QDir::currentPath() : pictures;
}

QString standardMoviesPath()
{
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    return movies.isEmpty() ? QDir::currentPath() : movies;
}

QString defaultOutputDirectory()
{
    return QDir::toNativeSeparators(QDir(standardMoviesPath()).filePath(QStringLiteral("C-Slice")));
}

QString systemSettingsPath()
{
    return defaultSliceDataPath(QStringLiteral("cslice.conf"));
}

QString presetsDirectoryPath()
{
    return defaultSliceDataPath(QStringLiteral("presets"));
}

QVariantMap readSettingsFile(const QString &path)
{
    QVariantMap values;
    if (!QFileInfo::exists(path)) {
        return values;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return values;
    }

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('['))) {
            continue;
        }

        const qsizetype separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }

        const QString key = line.left(separator).trimmed();
        const QString value = line.mid(separator + 1).trimmed();
        values.insert(key, value);
    }
    return values;
}

int mapInt(const QVariantMap &values, const QString &key, int fallback)
{
    bool ok = false;
    const int value = values.value(key, fallback).toString().toInt(&ok);
    return ok ? value : fallback;
}

double mapDouble(const QVariantMap &values, const QString &key, double fallback)
{
    bool ok = false;
    const double value = values.value(key, fallback).toString().toDouble(&ok);
    return ok ? value : fallback;
}

bool mapBool(const QVariantMap &values, const QString &key, bool fallback)
{
    const QString value = values.value(key, fallback).toString();
    if (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || value == QStringLiteral("1")) return true;
    if (value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0 || value == QStringLiteral("0")) return false;
    return fallback;
}

QString presetFilePath(QString presetName)
{
    presetName = QFileInfo(presetName.trimmed()).completeBaseName();
    if (presetName.isEmpty()) {
        return QString();
    }
    return QDir(presetsDirectoryPath()).filePath(presetName + QStringLiteral(".conf"));
}

bool writeSettingsFile(const QString &path, const QVariantMap &values)
{
    if (path.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    file.write("[ApplicationSettings]\n");
    const QStringList keys = {
        QStringLiteral("Configuration2D"), QStringLiteral("Configuration3D"), QStringLiteral("Format"),
        QStringLiteral("SurfaceRadius"), QStringLiteral("SurfaceFov"),
        QStringLiteral("LayerPitch"), QStringLiteral("LayerYaw"), QStringLiteral("LayerRoll"),
        QStringLiteral("PlaneAzimuth"), QStringLiteral("PlaneElevation"), QStringLiteral("PlaneRoll"),
        QStringLiteral("PlaneDistance"), QStringLiteral("PlaneHorizontal"), QStringLiteral("PlaneVertical"),
        QStringLiteral("PlaneWidth"), QStringLiteral("PlaneHeight"), QStringLiteral("MaxEncoderThreads"),
        QStringLiteral("ImageBufferingThreadCount"), QStringLiteral("CaptureGpuSlots"), QStringLiteral("ImageErrorBehavior"), QStringLiteral("Codec"),
        QStringLiteral("FrameRateNum"), QStringLiteral("FrameRateDen"), QStringLiteral("SoftwarePreset"), QStringLiteral("NvencPreset"), QStringLiteral("LibxTune"), QStringLiteral("NvencTune"), QStringLiteral("NvencHardwareFrames"),
        QStringLiteral("EncodingBitDepth"), QStringLiteral("CRF"), QStringLiteral("CQ"), QStringLiteral("QScale"),
        QStringLiteral("ParamFile"), QStringLiteral("FirstLeftInputLocation"),
        QStringLiteral("FirstRightInputLocation"), QStringLiteral("FirstOutputDirectoryLocation"), QStringLiteral("PreferMatroska"),
        QStringLiteral("UseOnlyIframes"), QStringLiteral("GopSizeSeconds")
    };
    for (const QString &key : keys) {
        const QString line = key + QLatin1Char('=') + values.value(key).toString() + QLatin1Char('\n');
        file.write(line.toUtf8());
    }
    return file.commit();
}

void applySystemSettingsFile()
{
    const QVariantMap values = readSettingsFile(systemSettingsPath());
    if (values.isEmpty()) {
        return;
    }

    if (values.contains(QStringLiteral("Configuration2D"))) SliceSettings::setConfiguration2D(values.value(QStringLiteral("Configuration2D")).toString());
    if (values.contains(QStringLiteral("Configuration3D"))) SliceSettings::setConfiguration3D(values.value(QStringLiteral("Configuration3D")).toString());
    if (values.contains(QStringLiteral("Format"))) SliceSettings::setFormat(values.value(QStringLiteral("Format")).toString());
    if (values.contains(QStringLiteral("SurfaceRadius"))) SliceSettings::setSurfaceRadius(mapDouble(values, QStringLiteral("SurfaceRadius"), SliceSettings::surfaceRadius()));
    if (values.contains(QStringLiteral("SurfaceFov"))) SliceSettings::setSurfaceFov(mapDouble(values, QStringLiteral("SurfaceFov"), SliceSettings::surfaceFov()));
    if (values.contains(QStringLiteral("LayerPitch"))) SliceSettings::setLayerPitch(mapDouble(values, QStringLiteral("LayerPitch"), SliceSettings::layerPitch()));
    if (values.contains(QStringLiteral("LayerYaw"))) SliceSettings::setLayerYaw(mapDouble(values, QStringLiteral("LayerYaw"), SliceSettings::layerYaw()));
    if (values.contains(QStringLiteral("LayerRoll"))) SliceSettings::setLayerRoll(mapDouble(values, QStringLiteral("LayerRoll"), SliceSettings::layerRoll()));
    if (values.contains(QStringLiteral("PlaneAzimuth"))) SliceSettings::setPlaneAzimuth(mapDouble(values, QStringLiteral("PlaneAzimuth"), SliceSettings::planeAzimuth()));
    if (values.contains(QStringLiteral("PlaneElevation"))) SliceSettings::setPlaneElevation(mapDouble(values, QStringLiteral("PlaneElevation"), SliceSettings::planeElevation()));
    if (values.contains(QStringLiteral("PlaneRoll"))) SliceSettings::setPlaneRoll(mapDouble(values, QStringLiteral("PlaneRoll"), SliceSettings::planeRoll()));
    if (values.contains(QStringLiteral("PlaneDistance"))) SliceSettings::setPlaneDistance(mapDouble(values, QStringLiteral("PlaneDistance"), SliceSettings::planeDistance()));
    if (values.contains(QStringLiteral("PlaneHorizontal"))) SliceSettings::setPlaneHorizontal(mapDouble(values, QStringLiteral("PlaneHorizontal"), SliceSettings::planeHorizontal()));
    if (values.contains(QStringLiteral("PlaneVertical"))) SliceSettings::setPlaneVertical(mapDouble(values, QStringLiteral("PlaneVertical"), SliceSettings::planeVertical()));
    if (values.contains(QStringLiteral("PlaneWidth"))) SliceSettings::setPlaneWidth(mapDouble(values, QStringLiteral("PlaneWidth"), SliceSettings::planeWidth()));
    if (values.contains(QStringLiteral("PlaneHeight"))) SliceSettings::setPlaneHeight(mapDouble(values, QStringLiteral("PlaneHeight"), SliceSettings::planeHeight()));
    if (values.contains(QStringLiteral("MaxEncoderThreads"))) SliceSettings::setMaxEncoderThreads(mapInt(values, QStringLiteral("MaxEncoderThreads"), SliceSettings::maxEncoderThreads()));
    if (values.contains(QStringLiteral("ImageBufferingThreadCount"))) SliceSettings::setImageBufferingThreadCount(mapInt(values, QStringLiteral("ImageBufferingThreadCount"), SliceSettings::imageBufferingThreadCount()));
    if (values.contains(QStringLiteral("CaptureGpuSlots"))) SliceSettings::setCaptureGpuSlots(mapInt(values, QStringLiteral("CaptureGpuSlots"), SliceSettings::captureGpuSlots()));
    if (values.contains(QStringLiteral("ImageErrorBehavior"))) SliceSettings::setImageErrorBehavior(values.value(QStringLiteral("ImageErrorBehavior")).toString());
    if (values.contains(QStringLiteral("Codec"))) SliceSettings::setCodec(values.value(QStringLiteral("Codec")).toString());
    if (values.contains(QStringLiteral("FrameRateNum"))) SliceSettings::setFrameRateNum(mapInt(values, QStringLiteral("FrameRateNum"), SliceSettings::frameRateNum()));
    if (values.contains(QStringLiteral("FrameRateDen"))) SliceSettings::setFrameRateDen(mapInt(values, QStringLiteral("FrameRateDen"), SliceSettings::frameRateDen()));
    if (values.contains(QStringLiteral("SoftwarePreset"))) SliceSettings::setSoftwarePreset(values.value(QStringLiteral("SoftwarePreset")).toString());
    if (values.contains(QStringLiteral("NvencPreset"))) SliceSettings::setNvencPreset(values.value(QStringLiteral("NvencPreset")).toString());
    if (values.contains(QStringLiteral("LibxTune"))) SliceSettings::setLibxTune(values.value(QStringLiteral("LibxTune")).toString());
    if (values.contains(QStringLiteral("NvencTune"))) SliceSettings::setNvencTune(values.value(QStringLiteral("NvencTune")).toString());
    if (values.contains(QStringLiteral("NvencHardwareFrames"))) SliceSettings::setNvencHardwareFrames(mapBool(values, QStringLiteral("NvencHardwareFrames"), SliceSettings::nvencHardwareFrames()));
    if (values.contains(QStringLiteral("EncodingBitDepth"))) SliceSettings::setEncodingBitDepth(mapInt(values, QStringLiteral("EncodingBitDepth"), SliceSettings::encodingBitDepth()));
    if (values.contains(QStringLiteral("CRF"))) SliceSettings::setCRF(mapInt(values, QStringLiteral("CRF"), SliceSettings::cRF()));
    if (values.contains(QStringLiteral("CQ"))) SliceSettings::setCQ(mapInt(values, QStringLiteral("CQ"), SliceSettings::cQ()));
    if (values.contains(QStringLiteral("QScale"))) SliceSettings::setQScale(mapInt(values, QStringLiteral("QScale"), SliceSettings::qScale()));
    if (values.contains(QStringLiteral("ParamFile"))) SliceSettings::setParamFile(values.value(QStringLiteral("ParamFile")).toString());
    if (values.contains(QStringLiteral("FirstLeftInputLocation"))) SliceSettings::setFirstLeftInputLocation(values.value(QStringLiteral("FirstLeftInputLocation")).toString());
    if (values.contains(QStringLiteral("FirstRightInputLocation"))) SliceSettings::setFirstRightInputLocation(values.value(QStringLiteral("FirstRightInputLocation")).toString());
    if (values.contains(QStringLiteral("FirstOutputDirectoryLocation"))) SliceSettings::setFirstOutputDirectoryLocation(values.value(QStringLiteral("FirstOutputDirectoryLocation")).toString());
    if (values.contains(QStringLiteral("PreferMatroska"))) SliceSettings::setPreferMatroska(mapBool(values, QStringLiteral("PreferMatroska"), SliceSettings::preferMatroska()));
    if (values.contains(QStringLiteral("UseOnlyIframes"))) SliceSettings::setUseOnlyIframes(mapBool(values, QStringLiteral("UseOnlyIframes"), SliceSettings::useOnlyIframes()));
    if (values.contains(QStringLiteral("GopSizeSeconds"))) SliceSettings::setGopSizeSeconds(mapDouble(values, QStringLiteral("GopSizeSeconds"), SliceSettings::gopSizeSeconds()));
}

QString directoryForDialogLocation(const QString &path, const QString &fallbackPath)
{
    const QString normalized = normalizedPath(path);
    const QString absolutePath = normalized.isEmpty() ? fallbackPath : absolutePathFromWorkingDirectory(normalized);
    const QFileInfo info(absolutePath);
    if (info.exists() && info.isFile()) {
        return QDir::toNativeSeparators(info.absolutePath());
    }
    return cleanNativePath(absolutePath);
}

QString parentDirectoryForFile(const QString &path)
{
    const QString absolutePath = absolutePathFromWorkingDirectory(path);
    if (absolutePath.isEmpty()) {
        return QString();
    }
    const QFileInfo info(absolutePath);
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    return cleanNativePath(directory);
}

QStringList outputNamesInConfiguration(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return {};
    }

    QStringList names;
    const QJsonArray nodes = document.object().value(QStringLiteral("nodes")).toArray();
    for (const QJsonValue &nodeValue : nodes) {
        const QJsonArray windows = nodeValue.toObject().value(QStringLiteral("windows")).toArray();
        for (const QJsonValue &windowValue : windows) {
            const QJsonObject window = windowValue.toObject();
            const int index = names.size();
            const QString fallbackName = QStringLiteral("Output %1").arg(index);
            names << window.value(QStringLiteral("name")).toString(fallbackName);
        }
    }
    return names;
}

struct ConfigFeatureFlags {
    bool warping = false;
    bool blendMask = false;
};

void scanConfigFeatures(const QJsonValue &value, ConfigFeatureFlags &features)
{
    if (features.warping && features.blendMask) {
        return;
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QString key = it.key();
            const bool hasValue = !it.value().isNull() && !it.value().isUndefined() && !(it.value().isString() && it.value().toString().trimmed().isEmpty());
            if (hasValue && key == QStringLiteral("mesh")) {
                features.warping = true;
            }
            else if (hasValue && (key == QStringLiteral("blendmask") || key == QStringLiteral("blacklevelmask"))) {
                features.blendMask = true;
            }
            scanConfigFeatures(it.value(), features);
        }
    }
    else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array) {
            scanConfigFeatures(entry, features);
        }
    }
}

ConfigFeatureFlags configFeatures(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    ConfigFeatureFlags features;
    scanConfigFeatures(document.isObject() ? QJsonValue(document.object()) : QJsonValue(document.array()), features);
    return features;
}

QString elapsedSecondsToTime(QString value)
{
    value = value.trimmed();
    if (value.endsWith(QLatin1Char('s'), Qt::CaseInsensitive)) {
        value.chop(1);
    }

    bool ok = false;
    const int totalSeconds = static_cast<int>(std::max(0.0, value.toDouble(&ok)));
    if (!ok) {
        return value;
    }

    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds / 60) % 60;
    const int seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString elapsedHmsToTime(const QString &hours, const QString &minutes, const QString &seconds)
{
    return QStringLiteral("%1:%2:%3")
        .arg(hours.toInt(), 2, 10, QLatin1Char('0'))
        .arg(minutes.toInt(), 2, 10, QLatin1Char('0'))
        .arg(seconds.toInt(), 2, 10, QLatin1Char('0'));
}

QString sequencePattern(const ImageSequenceScanResult &sequence)
{
    QString pattern = sequence.prefix;
    pattern += QString(sequence.digitCount, QLatin1Char('#'));
    if (!sequence.suffix.isEmpty()) {
        pattern += QLatin1Char('.') + sequence.suffix;
    }
    return pattern;
}

QString frameNumber(int frameIndex, int digitCount)
{
    return QStringLiteral("%1").arg(frameIndex, std::max(1, digitCount), 10, QLatin1Char('0'));
}

QString secondsToEstimate(qint64 totalSeconds)
{
    totalSeconds = std::max<qint64>(0, totalSeconds);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace

class SliceEstimateThread : public QThread {
public:
    using EstimateCallback = std::function<void(const QString &)>;

    enum class Mode {
        Render,
        Verify
    };

    explicit SliceEstimateThread(EstimateCallback callback, QObject *parent = nullptr)
        : QThread(parent)
        , m_callback(std::move(callback))
    {
    }

    ~SliceEstimateThread() override
    {
        stop();
    }

    void startEstimate(int totalFrames, Mode mode = Mode::Render)
    {
        QMutexLocker locker(&m_mutex);
        m_totalFrames = std::max(1, totalFrames);
        m_renderedFrames = 0;
        m_mode = mode;
        m_restartRequested = true;
        m_running = true;
        m_waitCondition.wakeOne();
    }

    void updateProgress(int renderedFrames, int totalFrames)
    {
        QMutexLocker locker(&m_mutex);
        m_renderedFrames = renderedFrames;
        m_totalFrames = std::max(1, totalFrames);
        m_progressChanged = true;
        m_waitCondition.wakeOne();
    }

    void resetEstimate()
    {
        QMutexLocker locker(&m_mutex);
        m_running = false;
        m_restartRequested = false;
        m_progressChanged = false;
        m_startedAt = 0;
        m_waitCondition.wakeOne();
    }

    void stop()
    {
        {
            QMutexLocker locker(&m_mutex);
            m_stopping = true;
            m_running = false;
            m_waitCondition.wakeOne();
        }
        quit();
        wait();
    }

protected:
    void run() override
    {
        while (true) {
            int totalFrames = 1;
            Mode mode = Mode::Render;

            {
                QMutexLocker locker(&m_mutex);
                while (!m_stopping && !m_restartRequested) {
                    m_waitCondition.wait(&m_mutex);
                }
                if (m_stopping) {
                    return;
                }

                totalFrames = m_totalFrames;
                mode = m_mode;
                m_restartRequested = false;
                m_progressChanged = false;
                m_startedAt = QDateTime::currentMSecsSinceEpoch();
            }

            publish(QStringLiteral("Calculating…"));

            while (true) {
                int renderedFrames = 0;
                qint64 startedAt = 0;
                {
                    QMutexLocker locker(&m_mutex);
                    if (!m_stopping && !m_restartRequested && !m_progressChanged && m_running) {
                        m_waitCondition.wait(&m_mutex, 1000);
                    }
                    if (m_stopping) {
                        return;
                    }
                    if (m_restartRequested) {
                        break;
                    }
                    if (!m_running) {
                        publish(QString());
                        break;
                    }

                    renderedFrames = m_renderedFrames;
                    totalFrames = std::max(1, m_totalFrames);
                    mode = m_mode;
                    startedAt = m_startedAt;
                    m_progressChanged = false;
                }

                publishEstimate(renderedFrames, totalFrames, startedAt, mode);
            }
        }
    }

private:
    bool m_useOnlyIframes = false;
    void publish(const QString &estimate)
    {
        if (m_callback) {
            m_callback(estimate);
        }
    }

    void publishEstimate(int renderedFrames, int totalFrames, qint64 startedAt, Mode mode)
    {
        if (startedAt <= 0) {
            return;
        }

        Q_UNUSED(mode)
        const qint64 elapsedMs = std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt);
        const qint64 minimumElapsedMs = 1000;
        if (elapsedMs < minimumElapsedMs) {
            publish(QStringLiteral("Calculating…"));
            return;
        }

        const double progressFraction = std::clamp(renderedFrames / static_cast<double>(totalFrames), 0.0, 1.0);
        if (progressFraction >= 0.999) {
            publish(QStringLiteral("00:00:00"));
            return;
        }
        if (progressFraction <= 0.0) {
            publish(QStringLiteral("Calculating…"));
            return;
        }

        const double elapsedSeconds = elapsedMs / 1000.0;
        const double remainingSeconds = (elapsedSeconds / progressFraction) - elapsedSeconds;
        if (!std::isfinite(remainingSeconds) || remainingSeconds < 0.0) {
            publish(QStringLiteral("Calculating…"));
            return;
        }

        publish(secondsToEstimate(static_cast<qint64>(std::ceil(remainingSeconds))));
    }

    EstimateCallback m_callback;
    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    bool m_stopping = false;
    bool m_running = false;
    bool m_restartRequested = false;
    bool m_progressChanged = false;
    int m_renderedFrames = 0;
    qint64 m_startedAt = 0;
    int m_totalFrames = 0;
    Mode m_mode = Mode::Render;
};

QVariantMap sequenceResultToMap(const ImageSequenceScanResult &sequence)
{
    QVariantMap values;
    values.insert(QStringLiteral("ok"), sequence.ok);
    values.insert(QStringLiteral("missingFrames"), sequence.missingFrames);
    values.insert(QStringLiteral("count"), sequence.count);
    values.insert(QStringLiteral("firstIndex"), sequence.firstIndex);
    values.insert(QStringLiteral("selectedIndex"), sequence.selectedIndex);
    values.insert(QStringLiteral("lastIndex"), sequence.lastIndex);
    values.insert(QStringLiteral("prefix"), sequence.prefix);
    values.insert(QStringLiteral("suffix"), sequence.suffix);
    values.insert(QStringLiteral("digitCount"), sequence.digitCount);
    values.insert(QStringLiteral("message"), sequence.message);
    QList<int> missingFrames = sequence.missingFrameIndices;
    values.insert(QStringLiteral("missingFrameIndices"), QVariant::fromValue(missingFrames));
    return values;
}

ImageSequenceScanResult sequenceResultFromMap(const QVariantMap &values)
{
    ImageSequenceScanResult sequence;
    sequence.ok = values.value(QStringLiteral("ok")).toBool();
    sequence.missingFrames = values.value(QStringLiteral("missingFrames")).toBool();
    sequence.missingFrameIndices = values.value(QStringLiteral("missingFrameIndices")).value<QList<int>>();
    sequence.count = values.value(QStringLiteral("count")).toInt();
    sequence.firstIndex = values.value(QStringLiteral("firstIndex")).toInt();
    sequence.selectedIndex = values.value(QStringLiteral("selectedIndex")).toInt();
    sequence.lastIndex = values.value(QStringLiteral("lastIndex")).toInt();
    sequence.prefix = values.value(QStringLiteral("prefix")).toString();
    sequence.suffix = values.value(QStringLiteral("suffix")).toString();
    sequence.digitCount = values.value(QStringLiteral("digitCount")).toInt();
    sequence.message = values.value(QStringLiteral("message")).toString();
    return sequence;
}

QVariantMap videoMetadataToMap(const CSlice::SliceVideoLoader::Metadata &metadata)
{
    QVariantMap values;
    values.insert(QStringLiteral("ok"), metadata.ok);
    values.insert(QStringLiteral("durationSeconds"), metadata.durationSeconds);
    values.insert(QStringLiteral("fps"), metadata.fps);
    values.insert(QStringLiteral("width"), metadata.width);
    values.insert(QStringLiteral("height"), metadata.height);
    values.insert(QStringLiteral("frameCount"), metadata.frameCount);
    values.insert(QStringLiteral("error"), QString::fromStdString(metadata.error));
    return values;
}

class ImageSequenceIndexThread : public QThread {
public:
    using ProgressCallback = std::function<void(int, int)>;
    using ResultCallback = std::function<void(int, bool, bool, bool, const QString&, const QString&, const QVariantMap&, const QVariantMap&)>;

    explicit ImageSequenceIndexThread(ProgressCallback progressCallback, ResultCallback resultCallback, QObject *parent = nullptr)
        : QThread(parent)
        , m_progressCallback(std::move(progressCallback))
        , m_resultCallback(std::move(resultCallback))
    {
    }

    ~ImageSequenceIndexThread() override
    {
        stop();
    }

    void requestIndex(int requestId, bool adoptDetectedRange, bool scanLeft, bool scanRight, const QString &leftPath, const QString &rightPath)
    {
        QMutexLocker locker(&m_mutex);
        m_requestId = requestId;
        m_adoptDetectedRange = adoptDetectedRange;
        m_scanLeft = scanLeft;
        m_scanRight = scanRight;
        m_leftPath = leftPath;
        m_rightPath = rightPath;
        m_restartRequested = true;
        m_waitCondition.wakeOne();
    }

    void stop()
    {
        {
            QMutexLocker locker(&m_mutex);
            m_stopping = true;
            m_waitCondition.wakeOne();
        }
        wait();
    }

protected:
    void run() override
    {
        while (true) {
            int requestId = 0;
            bool adoptDetectedRange = false;
            bool scanLeft = false;
            bool scanRight = false;
            QString leftPath;
            QString rightPath;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_stopping && !m_restartRequested) {
                    m_waitCondition.wait(&m_mutex);
                }
                if (m_stopping) {
                    return;
                }

                requestId = m_requestId;
                adoptDetectedRange = m_adoptDetectedRange;
                scanLeft = m_scanLeft;
                scanRight = m_scanRight;
                leftPath = m_leftPath;
                rightPath = m_rightPath;
                m_restartRequested = false;
            }

            QVariantMap leftSequence;
            if (scanLeft) {
                leftSequence = sequenceResultToMap(ImageSequenceUtils::scanImageSequence(leftPath, [this, requestId](int indexedFiles) {
                    publishProgress(requestId, indexedFiles);
                    return !requestRestartedOrStopping(requestId);
                }));
            }

            if (requestRestartedOrStopping(requestId)) {
                continue;
            }

            QVariantMap rightSequence;
            if (scanRight && !rightPath.isEmpty()) {
                rightSequence = sequenceResultToMap(ImageSequenceUtils::scanImageSequence(rightPath, [this, requestId](int indexedFiles) {
                    publishProgress(requestId, indexedFiles);
                    return !requestRestartedOrStopping(requestId);
                }));
            }

            if (requestRestartedOrStopping(requestId)) {
                continue;
            }

            if (m_resultCallback) {
                m_resultCallback(requestId, adoptDetectedRange, scanLeft, scanRight, leftPath, rightPath, leftSequence, rightSequence);
            }
        }
    }

private:
    bool requestRestartedOrStopping(int requestId) const
    {
        QMutexLocker locker(&m_mutex);
        return m_stopping || m_restartRequested || m_requestId != requestId;
    }

    void publishProgress(int requestId, int indexedFiles)
    {
        if (m_progressCallback) {
            m_progressCallback(requestId, indexedFiles);
        }
    }

    ProgressCallback m_progressCallback;
    ResultCallback m_resultCallback;
    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
    bool m_stopping = false;
    bool m_restartRequested = false;
    bool m_adoptDetectedRange = false;
    bool m_scanLeft = false;
    bool m_scanRight = false;
    int m_requestId = 0;
    QString m_leftPath;
    QString m_rightPath;
};

class VideoMetadataThread : public QThread {
public:
    using ResultCallback = std::function<void(int, bool, bool, bool, const QString&, const QString&, const QVariantMap&, const QVariantMap&)>;

    explicit VideoMetadataThread(ResultCallback resultCallback, QObject *parent = nullptr)
        : QThread(parent)
        , m_resultCallback(std::move(resultCallback))
    {
    }

    ~VideoMetadataThread() override
    {
        stop();
    }

    void requestMetadata(int requestId, bool adoptDetectedRange, bool probeLeft, bool probeRight, const QString &leftPath, const QString &rightPath)
    {
        QMutexLocker locker(&m_mutex);
        m_requestId = requestId;
        m_adoptDetectedRange = adoptDetectedRange;
        m_probeLeft = probeLeft;
        m_probeRight = probeRight;
        m_leftPath = leftPath;
        m_rightPath = rightPath;
        m_restartRequested = true;
        m_waitCondition.wakeOne();
    }

    void stop()
    {
        {
            QMutexLocker locker(&m_mutex);
            m_stopping = true;
            m_waitCondition.wakeOne();
        }
        wait();
    }

protected:
    void run() override
    {
        while (true) {
            int requestId = 0;
            bool adoptDetectedRange = false;
            bool probeLeft = false;
            bool probeRight = false;
            QString leftPath;
            QString rightPath;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_stopping && !m_restartRequested) {
                    m_waitCondition.wait(&m_mutex);
                }
                if (m_stopping) {
                    return;
                }
                requestId = m_requestId;
                adoptDetectedRange = m_adoptDetectedRange;
                probeLeft = m_probeLeft;
                probeRight = m_probeRight;
                leftPath = m_leftPath;
                rightPath = m_rightPath;
                m_restartRequested = false;
            }

            QVariantMap leftMetadata;
            if (probeLeft) {
                leftMetadata = videoMetadataToMap(CSlice::SliceVideoLoader::probeMetadata(filesystemPathFromQString(leftPath)));
            }

            if (requestRestartedOrStopping(requestId)) {
                continue;
            }

            QVariantMap rightMetadata;
            if (probeRight && !rightPath.isEmpty()) {
                rightMetadata = videoMetadataToMap(CSlice::SliceVideoLoader::probeMetadata(filesystemPathFromQString(rightPath)));
            }

            if (requestRestartedOrStopping(requestId)) {
                continue;
            }

            if (m_resultCallback) {
                m_resultCallback(requestId, adoptDetectedRange, probeLeft, probeRight, leftPath, rightPath, leftMetadata, rightMetadata);
            }
        }
    }

private:
    bool requestRestartedOrStopping(int requestId) const
    {
        QMutexLocker locker(&m_mutex);
        return m_stopping || m_restartRequested || m_requestId != requestId;
    }

    ResultCallback m_resultCallback;
    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
    bool m_stopping = false;
    bool m_restartRequested = false;
    bool m_adoptDetectedRange = false;
    bool m_probeLeft = false;
    bool m_probeRight = false;
    int m_requestId = 0;
    QString m_leftPath;
    QString m_rightPath;
};

SliceController::SliceController(QObject *parent)
    : QObject(parent)
{
    m_sliceEstimateThread = new SliceEstimateThread([this](const QString &estimate) {
        QMetaObject::invokeMethod(this, [this, estimate]() {
            setSliceRemainingTime(estimate);
        }, Qt::QueuedConnection);
    }, this);
    m_sliceEstimateThread->start();

    m_imageSequenceIndexThread = new ImageSequenceIndexThread([this](int requestId, int indexedFiles) {
        QMetaObject::invokeMethod(this, [this, requestId, indexedFiles]() {
            if (requestId != m_sequenceIndexRequestId) {
                return;
            }
            setSequenceStatus(QStringLiteral("Indexing images… %1 file(s)").arg(indexedFiles));
        }, Qt::QueuedConnection);
    }, [this](int requestId, bool adoptDetectedRange, bool scanLeft, bool scanRight, const QString &leftPath, const QString &rightPath, const QVariantMap &leftSequence, const QVariantMap &rightSequence) {
        QMetaObject::invokeMethod(this, [this, requestId, adoptDetectedRange, scanLeft, scanRight, leftPath, rightPath, leftSequence, rightSequence]() {
            applyImageSequenceStatus(requestId, adoptDetectedRange, scanLeft, scanRight, leftPath, rightPath, leftSequence, rightSequence);
        }, Qt::QueuedConnection);
    }, this);
    m_imageSequenceIndexThread->start();

    m_videoMetadataThread = new VideoMetadataThread([this](int requestId, bool adoptDetectedRange, bool probeLeft, bool probeRight, const QString &leftPath, const QString &rightPath, const QVariantMap &leftMetadata, const QVariantMap &rightMetadata) {
        QMetaObject::invokeMethod(this, [this, requestId, adoptDetectedRange, probeLeft, probeRight, leftPath, rightPath, leftMetadata, rightMetadata]() {
            if (requestId != m_sequenceIndexRequestId) {
                return;
            }
            setSequenceIndexing(false);
            if (probeLeft && !leftMetadata.value(QStringLiteral("ok")).toBool()) {
                m_indexedRightVideoLastFrame = -1;
                setSequenceStatus(leftMetadata.value(QStringLiteral("error"), QStringLiteral("Failed to read left video metadata.")).toString());
                return;
            }
            if (probeRight && !rightMetadata.value(QStringLiteral("ok")).toBool()) {
                m_indexedRightVideoLastFrame = -1;
                setSequenceStatus(rightMetadata.value(QStringLiteral("error"), QStringLiteral("Failed to read right video metadata.")).toString());
                return;
            }

            const int frameCount = leftMetadata.value(QStringLiteral("frameCount")).toInt();
            const int lastFrame = std::max(0, frameCount - 1);
            if (frameCount > 0) {
                const bool detectedRangeChanged = !m_hasIndexedRange || m_indexedStartIndex != 0 || m_indexedStopIndex != lastFrame;
                m_hasIndexedRange = true;
                m_indexedStartIndex = 0;
                m_indexedStopIndex = lastFrame;
                if (detectedRangeChanged) {
                    Q_EMIT indexedRangeChanged();
                }
                if (adoptDetectedRange) {
                    setStartIndex(0);
                    setStopIndex(lastFrame);
                }
            }

            const double detectedFps = leftMetadata.value(QStringLiteral("fps")).toDouble();
            if (detectedFps > 0.0) {
                // Find the closest standard frame rate preset within tolerance
                const double tolerance = SliceSettings::self()->inputFrameRateTolerance();

                struct FrameRatePreset { int num; int den; double value; };
                constexpr FrameRatePreset presets[] = {
                    { 30, 1, 30.0 },
                    { 60, 1, 60.0 },
                    { 30000, 1001, 30000.0 / 1001.0 },
                    { 60000, 1001, 60000.0 / 1001.0 }
                };

                int bestNum = 0;
                int bestDen = 0;
                double bestDiff = tolerance + 1.0; // Start worse than any valid match

                for (const auto &preset : presets) {
                    const double diff = qAbs(detectedFps - preset.value);
                    if (diff < bestDiff) {
                        bestDiff = diff;
                        bestNum = preset.num;
                        bestDen = preset.den;
                    }
                }

                if (bestNum > 0 && bestDiff <= tolerance) {
                    setInputFrameRateNum(bestNum);
                    setInputFrameRateDen(bestDen);
                } else {
                    // No preset within tolerance - use custom fraction
                    const int fpsNum = static_cast<int>(std::round(detectedFps * 1000));
                    setInputFrameRateNum(fpsNum);
                    setInputFrameRateDen(1000);
                }
            }

            if (probeRight && rightMetadata.value(QStringLiteral("ok")).toBool()) {
                const int rightFrameCount = rightMetadata.value(QStringLiteral("frameCount")).toInt();
                m_indexedRightVideoLastFrame = rightFrameCount > 0 ? std::max(0, rightFrameCount - 1) : -1;
            } else {
                m_indexedRightVideoLastFrame = -1;
            }

            QStringList details;
            details << QStringLiteral("left video %1 frames, %2x%3, %4 fps")
                .arg(frameCount)
                .arg(leftMetadata.value(QStringLiteral("width")).toInt())
                .arg(leftMetadata.value(QStringLiteral("height")).toInt())
                .arg(leftMetadata.value(QStringLiteral("fps")).toDouble(), 0, 'f', 3);
            if (probeRight && rightMetadata.value(QStringLiteral("ok")).toBool()) {
                if (rightMetadata.value(QStringLiteral("frameCount")).toInt() != frameCount ||
                    rightMetadata.value(QStringLiteral("width")).toInt() != leftMetadata.value(QStringLiteral("width")).toInt() ||
                    rightMetadata.value(QStringLiteral("height")).toInt() != leftMetadata.value(QStringLiteral("height")).toInt()) {
                    details << QStringLiteral("right video metadata differs (%1 frames, %2x%3)")
                        .arg(rightMetadata.value(QStringLiteral("frameCount")).toInt())
                        .arg(rightMetadata.value(QStringLiteral("width")).toInt())
                        .arg(rightMetadata.value(QStringLiteral("height")).toInt());
                }
                else {
                    details << QStringLiteral("right video matches");
                }
            }
            details << QStringLiteral("range %1-%2 step %3: %4 output frames")
                .arg(m_startIndex)
                .arg(m_stopIndex)
                .arg(std::max(1, m_steps))
                .arg(ImageSequenceUtils::expectedFrameCount(m_startIndex, m_stopIndex, m_steps));
            setSequenceStatus(details.join(QStringLiteral(" | ")));
            Q_EMIT sequencePreviewChanged();
        }, Qt::QueuedConnection);
    }, this);
    m_videoMetadataThread->start();

    m_configuration = defaultSliceDataPath(QStringLiteral("configs/default.json"));
    m_outputDirectory = defaultOutputDirectory();
    SliceSettings::self()->load();
    applyApplicationSettings();

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        handleProcessStdout(QString::fromLocal8Bit(m_process.readAllStandardOutput()));
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        handleProcessStderr(QString::fromLocal8Bit(m_process.readAllStandardError()));
    });
    connect(&m_process, &QProcess::started, this, [this]() {
        appendLog(QStringLiteral("Started C-Slice node process."));
        updateSliceProgress(0, 0, std::max(1, expectedSliceFrameCount()), m_startIndex, QStringLiteral("00:00:00"),
            m_verifyingSlice ? QStringLiteral("Verifying image headers.") : QStringLiteral("Processing media frames on the virtual dome."));
        Q_EMIT runningChanged();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        appendLog(QStringLiteral("Process error: %1").arg(m_process.errorString()));
        Q_EMIT runningChanged();
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
            handleProcessFinish();

            if (exitStatus == QProcess::NormalExit && exitCode == 0 && m_sliceTotalFrames > 0) {
                updateSliceProgress(m_sliceTotalFrames, m_sliceTotalFrames, m_sliceTotalFrames, m_sliceCurrentFrame, m_sliceElapsedTime,
                    m_verifyingSlice ? QStringLiteral("Verified.") : QStringLiteral("Completed."));
            }
            if (exitStatus == QProcess::NormalExit && exitCode == 0 && m_sliceTotalFrames > 0) {
                updateSliceProgress(m_sliceTotalFrames, m_sliceTotalFrames, m_sliceTotalFrames, m_sliceCurrentFrame, m_sliceElapsedTime,
                    m_verifyingSlice ? QStringLiteral("Verified.") : QStringLiteral("Completed."));
            }
        else if (m_sliceTotalFrames > 0 && m_lastSliceError.isEmpty()) {
                updateSliceProgress(m_sliceLoadedFrames, m_sliceRenderedFrames, m_sliceTotalFrames, m_sliceCurrentFrame, m_sliceElapsedTime, QStringLiteral("Stopped."));
            }
            appendLog(QStringLiteral("C-Slice node finished with exit code %1 (%2).")
                .arg(exitCode)
                .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crashed")));
            if (m_slicePaused) {
                m_slicePaused = false;
                Q_EMIT slicePausedChanged();
            }
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                setSliceRemainingTime(QStringLiteral("00:00:00"));
            }
            Q_EMIT runningChanged();
        });

    connect(&m_audioMuxProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        appendLog(QString::fromLocal8Bit(m_audioMuxProcess.readAllStandardOutput()).trimmed());
    });
    connect(&m_audioMuxProcess, &QProcess::readyReadStandardError, this, [this]() {
        appendLog(QString::fromLocal8Bit(m_audioMuxProcess.readAllStandardError()).trimmed());
    });
    connect(&m_audioMuxProcess, &QProcess::started, this, [this]() {
        appendLog(QStringLiteral("Started Audio Muxer process."));
        Q_EMIT audioMuxRunningChanged();
    });
    connect(&m_audioMuxProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        appendLog(QStringLiteral("Audio Muxer process error: %1").arg(m_audioMuxProcess.errorString()));
        Q_EMIT audioMuxRunningChanged();
    });
    connect(&m_audioMuxProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            appendLog(QStringLiteral("Audio Muxer finished with exit code %1 (%2).")
                .arg(exitCode)
                .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crashed")));
            Q_EMIT audioMuxRunningChanged();
        });
}

SliceController::~SliceController()
{
    if (m_sliceEstimateThread) {
        m_sliceEstimateThread->stop();
    }
    if (m_imageSequenceIndexThread) {
        m_imageSequenceIndexThread->stop();
    }
    if (m_videoMetadataThread) {
        m_videoMetadataThread->stop();
    }
}

void SliceController::handleProcessStderr(const QString &chunk)
{
    if (chunk.isEmpty()) {
        return;
    }

    m_processStderrBuffer += chunk;
    int newlineIndex = -1;
    while ((newlineIndex = m_processStderrBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = m_processStderrBuffer.left(newlineIndex);
        m_processStderrBuffer.remove(0, newlineIndex + 1);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }

        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        if (trimmed == m_lastSliceError || m_sliceCriticalErrors.contains(trimmed)) {
            continue;
        }

        appendLog(trimmed);
        if (trimmed.contains(QStringLiteral("Could not"), Qt::CaseInsensitive) ||
            trimmed.contains(QStringLiteral("Failed"), Qt::CaseInsensitive) ||
            trimmed.contains(QStringLiteral("Wuffs"), Qt::CaseInsensitive) ||
            trimmed.contains(QStringLiteral("Image file"), Qt::CaseInsensitive)) {
            setSliceError(trimmed);
        }
    }
}

QString SliceController::configuration() const { return m_configuration; }
void SliceController::setConfiguration(const QString &configuration)
{
    const QString value = relativePathIfInWorkingDirectory(normalizedPath(configuration));
    if (m_configuration == value) return;
    m_configuration = value;
    Q_EMIT configurationChanged();
    updateOutputCountFromConfiguration();
    updateConfigFeatureOptions();
    notifyCommandChanged();
}

QString SliceController::inputType() const { return m_inputType; }
void SliceController::setInputType(const QString &inputType)
{
    const QString value = normalizedInputType(inputType);
    if (m_inputType == value) return;
    m_inputType = value;
    Q_EMIT inputTypeChanged();
    if (m_inputType == QStringLiteral("Image sequence")) {
        refreshImageSequenceStatus(true, true, m_stereo && !m_rightInput.trimmed().isEmpty());
    }
    else {
        refreshVideoMetadataStatus(true, true, m_stereo && !m_rightInput.trimmed().isEmpty());
    }
    notifyCommandChanged();
    Q_EMIT sequencePreviewChanged();
}

void SliceController::updateConfigFeatureOptions()
{
    const ConfigFeatureFlags features = configFeatures(configuredSlicePath(m_configuration, QStringLiteral("configs/default.json")));
    if (features.warping && !m_warping) {
        m_warping = true;
        Q_EMIT warpingChanged();
    }
    if (features.blendMask && !m_blendMask) {
        m_blendMask = true;
        Q_EMIT blendMaskChanged();
    }
}

QString SliceController::leftInput() const { return m_leftInput; }
void SliceController::setLeftInput(const QString &leftInput)
{
    const QString value = normalizedPath(leftInput);
    const bool changed = m_leftInput != value;
    if (changed) {
        m_leftInput = value;
        Q_EMIT leftInputChanged();
    }
    if (!value.isEmpty()) {
        setLeftInputDialogLocation(parentDirectoryForFile(value));
        if (m_inputType == QStringLiteral("Image sequence")) {
            refreshImageSequenceStatus(true, true, false);
        }
        else {
            refreshVideoMetadataStatus(true, true, false);
        }
        Q_EMIT sequencePreviewChanged();
    }
    if (changed) {
        notifyCommandChanged();
    }

}

QString SliceController::rightInput() const { return m_rightInput; }
void SliceController::setRightInput(const QString &rightInput)
{
    const QString value = normalizedPath(rightInput);
    const bool changed = m_rightInput != value;
    if (changed) {
        m_rightInput = value;
        Q_EMIT rightInputChanged();
    }
    if (!value.isEmpty()) {
        setRightInputDialogLocation(parentDirectoryForFile(value));
        if (m_inputType == QStringLiteral("Image sequence")) {
            refreshImageSequenceStatus(true, false, true);
        }
        else {
            refreshVideoMetadataStatus(true, false, true);
        }
        Q_EMIT sequencePreviewChanged();
    }
    if (changed) {
        notifyCommandChanged();
    }
}

QString SliceController::outputDirectory() const { return m_outputDirectory; }
void SliceController::setOutputDirectory(const QString &outputDirectory)
{
    const QString value = relativePathIfInWorkingDirectory(normalizedPath(outputDirectory));
    if (m_outputDirectory == value) return;
    m_outputDirectory = value;
    Q_EMIT outputDirectoryChanged();
    setOutputDirectoryDialogLocation(absolutePathFromWorkingDirectory(value));
    const QString directoryName = QFileInfo(value).fileName();
    if (!directoryName.isEmpty()) {
        setOutputName(directoryName);
    }
    notifyCommandChanged();
}

QString SliceController::outputName() const { return m_outputName; }
void SliceController::setOutputName(const QString &outputName)
{
    const QString value = outputName.trimmed().isEmpty() ? QStringLiteral("slice") : outputName.trimmed();
    if (m_outputName == value) return;
    m_outputName = value;
    Q_EMIT outputNameChanged();
    notifyCommandChanged();
}

QString SliceController::leftInputDialogLocation() const { return m_leftInputDialogLocation; }
QString SliceController::rightInputDialogLocation() const { return m_rightInputDialogLocation; }
QString SliceController::outputDirectoryDialogLocation() const { return m_outputDirectoryDialogLocation; }

bool SliceController::stereo() const { return m_stereo; }
void SliceController::setStereo(bool stereo)
{
    if (m_stereo == stereo) return;
    m_stereo = stereo;
    Q_EMIT stereoChanged();
    const QString config = m_stereo ? SliceSettings::configuration3D() : SliceSettings::configuration2D();
    const QString fallbackConfig = m_stereo ? QStringLiteral("configs/nrkp_stereo_2026.json") : QStringLiteral("configs/nrkp_mono_2026.json");
    setConfiguration(configuredSlicePath(config, fallbackConfig));
    if (m_inputType == QStringLiteral("Video")) {
        refreshVideoMetadataStatus(false, true, m_stereo && !m_rightInput.trimmed().isEmpty());
    }
    if (m_leftInput.trimmed().isEmpty()) {
        setSequenceStatus(QString());
    }
    else if (m_inputType == QStringLiteral("Image sequence") && !m_lastIndexedLeftSequence.isEmpty()) {
        updateSequenceStatusFromScans(false,
            absolutePathFromWorkingDirectory(m_leftInput),
            (m_stereo && !m_rightInput.trimmed().isEmpty()) ? absolutePathFromWorkingDirectory(m_rightInput) : QString(),
            m_lastIndexedLeftSequence,
            m_stereo ? m_lastIndexedRightSequence : QVariantMap());
    }
    notifyCommandChanged();
    Q_EMIT sequencePreviewChanged();
}

bool SliceController::upsideDown() const { return m_upsideDown; }
void SliceController::setUpsideDown(bool upsideDown)
{
    if (m_upsideDown == upsideDown) return;
    m_upsideDown = upsideDown;
    Q_EMIT upsideDownChanged();
    notifyCommandChanged();
}

bool SliceController::warping() const { return m_warping; }
void SliceController::setWarping(bool warping)
{
    if (m_warping == warping) return;
    m_warping = warping;
    Q_EMIT warpingChanged();
    notifyCommandChanged();
}

bool SliceController::blendMask() const { return m_blendMask; }
void SliceController::setBlendMask(bool blendMask)
{
    if (m_blendMask == blendMask) return;
    m_blendMask = blendMask;
    Q_EMIT blendMaskChanged();
    notifyCommandChanged();
}

int SliceController::startIndex() const { return m_startIndex; }
void SliceController::setStartIndex(int startIndex)
{
    if (m_startIndex == startIndex) return;
    m_startIndex = startIndex;
    Q_EMIT startIndexChanged();
    notifyCommandChanged();
    Q_EMIT sequencePreviewChanged();
}

int SliceController::stopIndex() const { return m_stopIndex; }
void SliceController::setStopIndex(int stopIndex)
{
    if (m_stopIndex == stopIndex) return;
    m_stopIndex = stopIndex;
    Q_EMIT stopIndexChanged();
    notifyCommandChanged();
    Q_EMIT sequencePreviewChanged();
}

int SliceController::steps() const { return m_steps; }
void SliceController::setSteps(int steps)
{
    const int value = std::max(1, steps);
    if (m_steps == value) return;
    m_steps = value;
    Q_EMIT stepsChanged();
    notifyCommandChanged();
    Q_EMIT sequencePreviewChanged();
}

int SliceController::outputCount() const { return m_outputCount; }
void SliceController::setOutputCount(int outputCount)
{
    const int value = std::max(1, outputCount);
    QStringList names;
    names.reserve(value);
    for (int i = 0; i < value; ++i) {
        names << QStringLiteral("Output %1").arg(i);
    }
    setOutputNames(names);
}

int SliceController::selectedOutputCount() const
{
    int count = 0;
    for (bool enabled : m_outputEnabled) {
        if (enabled) {
            ++count;
        }
    }
    return count;
}

QVariantList SliceController::outputs() const
{
    QVariantList result;
    result.reserve(m_outputNames.size());
    for (int i = 0; i < m_outputNames.size(); ++i) {
        QVariantMap output;
        output.insert(QStringLiteral("index"), i);
        output.insert(QStringLiteral("name"), m_outputNames.at(i));
        output.insert(QStringLiteral("channel"), outputIdentifier(i));
        output.insert(QStringLiteral("identifier"), outputIdentifier(i));
        output.insert(QStringLiteral("enabled"), outputEnabled(i));
        result << output;
    }
    return result;
}

int SliceController::maxEncoderThreads() const { return m_maxEncoderThreads; }
void SliceController::setMaxEncoderThreads(int maxEncoderThreads)
{
    const int value = maxEncoderThreads <= 0 ? 16 : std::clamp(maxEncoderThreads, 1, 128);
    if (m_maxEncoderThreads == value) return;
    m_maxEncoderThreads = value;
    Q_EMIT maxEncoderThreadsChanged();
    notifyCommandChanged();
}

int SliceController::imageBufferingThreadCount() const { return m_imageBufferingThreadCount; }
void SliceController::setImageBufferingThreadCount(int imageBufferingThreadCount)
{
    const int value = std::clamp(imageBufferingThreadCount, 1, 64);
    if (m_imageBufferingThreadCount == value) return;
    m_imageBufferingThreadCount = value;
    Q_EMIT imageBufferingThreadCountChanged();
    notifyCommandChanged();
}

int SliceController::captureGpuSlots() const { return m_captureGpuSlots; }
void SliceController::setCaptureGpuSlots(int captureGpuSlots)
{
    const int value = std::clamp(captureGpuSlots, 1, 128);
    if (m_captureGpuSlots == value) return;
    m_captureGpuSlots = value;
    Q_EMIT captureGpuSlotsChanged();
    notifyCommandChanged();
}

int SliceController::imageSizeWarningPercent() const { return m_imageSizeWarningPercent; }
void SliceController::setImageSizeWarningPercent(int imageSizeWarningPercent)
{
    const int value = std::clamp(imageSizeWarningPercent, 0, 100);
    if (m_imageSizeWarningPercent == value) return;
    m_imageSizeWarningPercent = value;
    Q_EMIT imageSizeWarningPercentChanged();
    notifyCommandChanged();
}

bool SliceController::runWithoutEncoding() const { return m_runWithoutEncoding; }
void SliceController::setRunWithoutEncoding(bool runWithoutEncoding)
{
    if (m_runWithoutEncoding == runWithoutEncoding) return;
    m_runWithoutEncoding = runWithoutEncoding;
    if (!m_runWithoutEncoding && m_runWithoutReadback) {
        m_runWithoutReadback = false;
        Q_EMIT runWithoutReadbackChanged();
    }
    Q_EMIT runWithoutEncodingChanged();
    notifyCommandChanged();
}

bool SliceController::runWithoutReadback() const { return m_runWithoutReadback; }
void SliceController::setRunWithoutReadback(bool runWithoutReadback)
{
    const bool value = m_runWithoutEncoding && runWithoutReadback;
    if (m_runWithoutReadback == value) return;
    m_runWithoutReadback = value;
    Q_EMIT runWithoutReadbackChanged();
    notifyCommandChanged();
}


QString SliceController::mappingMode() const { return m_mappingMode; }
void SliceController::setMappingMode(const QString &mappingMode)
{
    const QString value = normalizedMappingMode(mappingMode);
    if (m_mappingMode == value) return;
    m_mappingMode = value;
    Q_EMIT mappingModeChanged();
    notifyCommandChanged();
}

double SliceController::surfaceRadius() const { return m_surfaceRadius; }
void SliceController::setSurfaceRadius(double surfaceRadius)
{
    const double value = std::max(1.0, surfaceRadius);
    if (qFuzzyCompare(m_surfaceRadius, value)) return;
    m_surfaceRadius = value;
    Q_EMIT surfaceRadiusChanged();
    notifyCommandChanged();
}

double SliceController::surfaceFov() const { return m_surfaceFov; }
void SliceController::setSurfaceFov(double surfaceFov)
{
    const double value = std::clamp(surfaceFov, 1.0, 360.0);
    if (qFuzzyCompare(m_surfaceFov, value)) return;
    m_surfaceFov = value;
    Q_EMIT surfaceFovChanged();
    notifyCommandChanged();
}

QString SliceController::layerStereoMode() const { return m_layerStereoMode; }
void SliceController::setLayerStereoMode(const QString &layerStereoMode)
{
    const QString value = normalizedLayerStereoMode(layerStereoMode);
    if (m_layerStereoMode == value) return;
    m_layerStereoMode = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

int SliceController::layerAlpha() const { return m_layerAlpha; }
void SliceController::setLayerAlpha(int layerAlpha)
{
    const int value = std::clamp(layerAlpha, 0, 100);
    if (m_layerAlpha == value) return;
    m_layerAlpha = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

bool SliceController::layerRoiEnabled() const { return m_layerRoiEnabled; }
void SliceController::setLayerRoiEnabled(bool layerRoiEnabled)
{
    if (m_layerRoiEnabled == layerRoiEnabled) return;
    m_layerRoiEnabled = layerRoiEnabled;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::layerRoiX() const { return m_layerRoiX; }
void SliceController::setLayerRoiX(double layerRoiX)
{
    const double value = std::min(clampedRoiCoordinate(layerRoiX), 1.0 - m_layerRoiWidth);
    if (qFuzzyCompare(m_layerRoiX, value)) return;
    m_layerRoiX = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::layerRoiY() const { return m_layerRoiY; }
void SliceController::setLayerRoiY(double layerRoiY)
{
    const double value = std::min(clampedRoiCoordinate(layerRoiY), 1.0 - m_layerRoiHeight);
    if (qFuzzyCompare(m_layerRoiY, value)) return;
    m_layerRoiY = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::layerRoiWidth() const { return m_layerRoiWidth; }
void SliceController::setLayerRoiWidth(double layerRoiWidth)
{
    const double value = std::min(clampedRoiSize(layerRoiWidth), 1.0 - m_layerRoiX);
    if (qFuzzyCompare(m_layerRoiWidth, value)) return;
    m_layerRoiWidth = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::layerRoiHeight() const { return m_layerRoiHeight; }
void SliceController::setLayerRoiHeight(double layerRoiHeight)
{
    const double value = std::min(clampedRoiSize(layerRoiHeight), 1.0 - m_layerRoiY);
    if (qFuzzyCompare(m_layerRoiHeight, value)) return;
    m_layerRoiHeight = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::layerPitch() const { return m_layerPitch; }
void SliceController::setLayerPitch(double layerPitch)
{
    const double value = std::clamp(layerPitch, -360.0, 360.0);
    if (qFuzzyCompare(m_layerPitch, value)) return;
    m_layerPitch = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::layerYaw() const { return m_layerYaw; }
void SliceController::setLayerYaw(double layerYaw)
{
    const double value = std::clamp(layerYaw, -360.0, 360.0);
    if (qFuzzyCompare(m_layerYaw, value)) return;
    m_layerYaw = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::layerRoll() const { return m_layerRoll; }
void SliceController::setLayerRoll(double layerRoll)
{
    const double value = std::clamp(layerRoll, -360.0, 360.0);
    if (qFuzzyCompare(m_layerRoll, value)) return;
    m_layerRoll = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::planeAzimuth() const { return m_planeAzimuth; }
void SliceController::setPlaneAzimuth(double planeAzimuth)
{
    const double value = std::clamp(planeAzimuth, -360.0, 360.0);
    if (qFuzzyCompare(m_planeAzimuth, value)) return;
    m_planeAzimuth = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::planeElevation() const { return m_planeElevation; }
void SliceController::setPlaneElevation(double planeElevation)
{
    const double value = std::clamp(planeElevation, -180.0, 180.0);
    if (qFuzzyCompare(m_planeElevation, value)) return;
    m_planeElevation = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::planeRoll() const { return m_planeRoll; }
void SliceController::setPlaneRoll(double planeRoll)
{
    const double value = std::clamp(planeRoll, -360.0, 360.0);
    if (qFuzzyCompare(m_planeRoll, value)) return;
    m_planeRoll = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::planeDistance() const { return m_planeDistance; }
void SliceController::setPlaneDistance(double planeDistance)
{
    const double value = std::max(1.0, planeDistance);
    if (qFuzzyCompare(m_planeDistance, value)) return;
    m_planeDistance = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::planeHorizontal() const { return m_planeHorizontal; }
void SliceController::setPlaneHorizontal(double planeHorizontal)
{
    if (qFuzzyCompare(m_planeHorizontal, planeHorizontal)) return;
    m_planeHorizontal = planeHorizontal;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::planeVertical() const { return m_planeVertical; }
void SliceController::setPlaneVertical(double planeVertical)
{
    if (qFuzzyCompare(m_planeVertical, planeVertical)) return;
    m_planeVertical = planeVertical;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::planeWidth() const { return m_planeWidth; }
void SliceController::setPlaneWidth(double planeWidth)
{
    const double value = std::max(0.0, planeWidth);
    if (qFuzzyCompare(m_planeWidth, value)) return;
    m_planeWidth = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

double SliceController::planeHeight() const { return m_planeHeight; }
void SliceController::setPlaneHeight(double planeHeight)
{
    const double value = std::max(0.0, planeHeight);
    if (qFuzzyCompare(m_planeHeight, value)) return;
    m_planeHeight = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}

int SliceController::planeAspectRatio() const { return m_planeAspectRatio; }
void SliceController::setPlaneAspectRatio(int planeAspectRatio)
{
    const int value = std::clamp(planeAspectRatio, 0, 2);
    if (m_planeAspectRatio == value) return;
    m_planeAspectRatio = value;
    Q_EMIT layerSettingsChanged();
    notifyCommandChanged();
}


QString SliceController::codec() const { return m_codec; }
void SliceController::setCodec(const QString &codec)
{
    if (m_codec == codec) return;
    m_codec = codec;
    Q_EMIT codecChanged();
    Q_EMIT presetChanged();
    Q_EMIT outputContainerSuffixesChanged();
    Q_EMIT outputContainerSuffixChanged();
    notifyCommandChanged();
}

QString SliceController::preset() const { return isNvencCodecName(m_codec) ? m_nvencPreset : m_softwarePreset; }
void SliceController::setPreset(const QString &preset)
{
    if (isNvencCodecName(m_codec)) {
        setNvencPreset(preset);
    }
    else {
        setSoftwarePreset(preset);
    }
}

QString SliceController::softwarePreset() const { return m_softwarePreset; }
void SliceController::setSoftwarePreset(const QString &softwarePreset)
{
    const QString value = softwarePreset.trimmed().isEmpty() ? QStringLiteral("veryslow") : softwarePreset.trimmed();
    if (m_softwarePreset == value) return;
    m_softwarePreset = value;
    Q_EMIT softwarePresetChanged();
    if (!isNvencCodecName(m_codec)) {
        Q_EMIT presetChanged();
    }
    notifyCommandChanged();
}

QString SliceController::nvencPreset() const { return m_nvencPreset; }
void SliceController::setNvencPreset(const QString &nvencPreset)
{
    const QString value = nvencPreset.trimmed().isEmpty() ? QStringLiteral("High quality (P6)") : nvencPreset.trimmed();
    if (m_nvencPreset == value) return;
    m_nvencPreset = value;
    Q_EMIT nvencPresetChanged();
    if (isNvencCodecName(m_codec)) {
        Q_EMIT presetChanged();
    }
    notifyCommandChanged();
}

QString SliceController::libxTune() const { return m_libxTune; }
void SliceController::setLibxTune(const QString &libxTune)
{
    const QString value = libxTune.trimmed().isEmpty() ? QStringLiteral("none") : libxTune.trimmed();
    if (m_libxTune == value) return;
    m_libxTune = value;
    Q_EMIT libxTuneChanged();
    notifyCommandChanged();
}

QString SliceController::nvencTune() const { return m_nvencTune; }
void SliceController::setNvencTune(const QString &nvencTune)
{
    const QString value = nvencTune.trimmed().isEmpty() ? QStringLiteral("High quality") : nvencTune.trimmed();
    if (m_nvencTune == value) return;
    m_nvencTune = value;
    Q_EMIT nvencTuneChanged();
    notifyCommandChanged();
}

bool SliceController::nvencHardwareFrames() const { return m_nvencHardwareFrames; }
void SliceController::setNvencHardwareFrames(bool nvencHardwareFrames)
{
    if (m_nvencHardwareFrames == nvencHardwareFrames) return;
    m_nvencHardwareFrames = nvencHardwareFrames;
    Q_EMIT nvencHardwareFramesChanged();
    notifyCommandChanged();
}

int SliceController::encodingBitDepth() const { return m_encodingBitDepth; }
void SliceController::setEncodingBitDepth(int encodingBitDepth)
{
    const int value = encodingBitDepth >= 10 ? 10 : 8;
    if (m_encodingBitDepth == value) return;
    m_encodingBitDepth = value;
    Q_EMIT encodingBitDepthChanged();
    notifyCommandChanged();
}

int SliceController::pixrate() const { return m_pixrate; }
void SliceController::setPixrate(int pixrate)
{
    const int value = std::max(1, pixrate);
    if (m_pixrate == value) return;
    m_pixrate = value;
    Q_EMIT pixrateChanged();
    notifyCommandChanged();
}

int SliceController::constantQuality() const
{
    if (isNvencCodecName(m_codec)) {
        return m_cq;
    }
    if (isCrfCodecName(m_codec)) {
        return m_crf;
    }
    return m_qscale;
}
void SliceController::setConstantQuality(int constantQuality)
{
    const int value = std::clamp(constantQuality, 0, 51);
    if (m_constantQuality == value) return;
    m_constantQuality = value;
    Q_EMIT constantQualityChanged();
    notifyCommandChanged();
}

int SliceController::crf() const { return m_crf; }
void SliceController::setCrf(int crf)
{
    const int value = std::clamp(crf, 0, 51);
    if (m_crf == value) return;
    m_crf = value;
    Q_EMIT crfChanged();
    Q_EMIT constantQualityChanged();
    notifyCommandChanged();
}

int SliceController::cq() const { return m_cq; }
void SliceController::setCq(int cq)
{
    const int value = std::clamp(cq, 0, 51);
    if (m_cq == value) return;
    m_cq = value;
    Q_EMIT cqChanged();
    Q_EMIT constantQualityChanged();
    notifyCommandChanged();
}

int SliceController::qscale() const { return m_qscale; }
void SliceController::setQscale(int qscale)
{
    const int value = std::clamp(qscale, 0, 51);
    if (m_qscale == value) return;
    m_qscale = value;
    Q_EMIT qscaleChanged();
    Q_EMIT constantQualityChanged();
    notifyCommandChanged();
}

int SliceController::frameRateNum() const { return m_frameRateNum; }
void SliceController::setFrameRateNum(int frameRateNum)
{
    const int value = std::max(1, frameRateNum);
    if (m_frameRateNum == value) return;
    m_frameRateNum = value;
    Q_EMIT frameRateNumChanged();
    notifyCommandChanged();
}

int SliceController::frameRateDen() const { return m_frameRateDen; }
void SliceController::setFrameRateDen(int frameRateDen)
{
    const int value = std::max(1, frameRateDen);
    if (m_frameRateDen == value) return;
    m_frameRateDen = value;
    Q_EMIT frameRateDenChanged();
    notifyCommandChanged();
}

int SliceController::inputFrameRateNum() const { return m_inputFrameRateNum; }
void SliceController::setInputFrameRateNum(int inputFrameRateNum)
{
    const int value = std::max(1, inputFrameRateNum);
    if (m_inputFrameRateNum == value) return;
    m_inputFrameRateNum = value;
    Q_EMIT inputFrameRateNumChanged();
    notifyCommandChanged();
}

int SliceController::inputFrameRateDen() const { return m_inputFrameRateDen; }
void SliceController::setInputFrameRateDen(int inputFrameRateDen)
{
    const int value = std::max(1, inputFrameRateDen);
    if (m_inputFrameRateDen == value) return;
    m_inputFrameRateDen = value;
    Q_EMIT inputFrameRateDenChanged();
    notifyCommandChanged();
}

QString SliceController::parameterFile() const { return m_parameterFile; }
void SliceController::setParameterFile(const QString &parameterFile)
{
    const QString value = relativePathIfInWorkingDirectory(normalizedPath(parameterFile));
    if (m_parameterFile == value) return;
    m_parameterFile = value;
    Q_EMIT parameterFileChanged();
    notifyCommandChanged();
}

bool SliceController::preferMatroska() const { return m_preferMatroska; }
void SliceController::setPreferMatroska(bool preferMatroska)
{
    if (m_preferMatroska == preferMatroska) return;
    m_preferMatroska = preferMatroska;
    Q_EMIT preferMatroskaChanged();
    Q_EMIT outputContainerSuffixChanged();
    notifyCommandChanged();
}

QString SliceController::outputContainerSuffix() const
{
    const QStringList suffixes = outputContainerSuffixes();
    if (m_preferMatroska && suffixes.contains(QStringLiteral(".mkv"))) {
        return QStringLiteral(".mkv");
    }
    return suffixes.isEmpty() ? QString() : suffixes.first();
}

void SliceController::setOutputContainerSuffix(const QString &outputContainerSuffix)
{
    const QString value = outputContainerSuffix.trimmed().startsWith(QLatin1Char('.'))
        ? outputContainerSuffix.trimmed()
        : QStringLiteral(".") + outputContainerSuffix.trimmed();
    if (!outputContainerSuffixes().contains(value)) {
        return;
    }
    setPreferMatroska(value == QStringLiteral(".mkv"));
}

QStringList SliceController::outputContainerSuffixes() const
{
    return outputContainerSuffixesForCodec(m_codec);
}

bool SliceController::running() const { return m_process.state() != QProcess::NotRunning; }
bool SliceController::slicePaused() const { return m_slicePaused; }
QString SliceController::imageErrorBehavior() const { return m_imageErrorBehavior; }
void SliceController::setImageErrorBehavior(const QString &imageErrorBehavior)
{
    QString value = imageErrorBehavior.trimmed();
    if (value != QStringLiteral("Abort") && value != QStringLiteral("Pause") && value != QStringLiteral("Continue")) {
        value = QStringLiteral("Abort");
    }
    if (m_imageErrorBehavior == value) return;
    m_imageErrorBehavior = value;
    Q_EMIT imageErrorBehaviorChanged();
    notifyCommandChanged();
}

void SliceController::setVideoDecodingMode(const QString &videoDecodingMode)
{
    QString value = videoDecodingMode.trimmed();
    if (value != QStringLiteral("Software") && value != QStringLiteral("Hardware") && value != QStringLiteral("Hybrid")) {
        value = QStringLiteral("Software");
    }
    if (m_videoDecodingMode == value) return;
    m_videoDecodingMode = value;
    Q_EMIT videoDecodingModeChanged();
    notifyCommandChanged();
}

bool SliceController::audioMuxRunning() const { return m_audioMuxProcess.state() != QProcess::NotRunning; }
QStringList SliceController::presetNames() const
{
    QDir presetsDir(presetsDirectoryPath());
    const QFileInfoList files = presetsDir.entryInfoList({ QStringLiteral("*.conf") }, QDir::Files, QDir::Name | QDir::IgnoreCase);
    QStringList names;
    names.reserve(files.size());
    for (const QFileInfo &file : files) {
        names << file.completeBaseName();
    }
    return names;
}
QString SliceController::logText() const { return m_logText; }
QString SliceController::sequenceStatus() const { return m_sequenceStatus; }
bool SliceController::sequenceIndexing() const { return m_sequenceIndexing; }
bool SliceController::hasIndexedRange() const { return m_hasIndexedRange; }
int SliceController::indexedStartIndex() const { return m_indexedStartIndex; }
int SliceController::indexedStopIndex() const { return m_indexedStopIndex; }
int SliceController::sliceProgress() const { return m_sliceRenderedProgress; }
int SliceController::sliceProcessedFrames() const { return m_sliceRenderedFrames; }
int SliceController::sliceLoadedProgress() const { return m_sliceLoadedProgress; }
int SliceController::sliceRenderedProgress() const { return m_sliceRenderedProgress; }
int SliceController::sliceLoadedFrames() const { return m_sliceLoadedFrames; }
int SliceController::sliceRenderedFrames() const { return m_sliceRenderedFrames; }
int SliceController::sliceTotalFrames() const { return m_sliceTotalFrames; }
int SliceController::sliceCurrentFrame() const { return m_sliceCurrentFrame; }
QString SliceController::sliceElapsedTime() const { return m_sliceElapsedTime; }
QString SliceController::sliceRemainingTime() const { return m_sliceRemainingTime; }
QString SliceController::sliceProgressStatus() const { return m_sliceProgressStatus; }
QString SliceController::sliceProgressAction() const { return m_sliceProgressAction; }
QString SliceController::sliceCriticalErrors() const { return m_sliceCriticalErrors.join(QLatin1Char('\n')); }
QVariantList SliceController::sliceFailedFiles() const { return m_sliceFailedFiles; }

bool SliceController::sequencePreviewRight() const { return m_sequencePreviewRight; }
void SliceController::setSequencePreviewRight(bool sequencePreviewRight)
{
    if (m_sequencePreviewRight == sequencePreviewRight) return;
    m_sequencePreviewRight = sequencePreviewRight;
    Q_EMIT sequencePreviewChanged();
}

int SliceController::sequencePreviewMinimum() const
{
    if (m_inputType == QStringLiteral("Video")) {
        return std::min(m_startIndex, m_stopIndex);
    }

    const QString input = m_sequencePreviewRight ? m_rightInput : m_leftInput;
    if (input.trimmed().isEmpty()) {
        return 0;
    }

    const ImageSequenceScanResult sequence = ImageSequenceUtils::parseImageSequencePattern(absolutePathFromWorkingDirectory(input));
    return sequence.ok ? std::min(m_startIndex, m_stopIndex) : 0;
}

int SliceController::sequencePreviewMaximum() const
{
    if (m_inputType == QStringLiteral("Video")) {
        const int baseMax = std::max(m_startIndex, m_stopIndex);
        if (m_sequencePreviewRight && m_indexedRightVideoLastFrame >= 0) {
            return std::min(baseMax, m_indexedRightVideoLastFrame);
        }
        return baseMax;
    }

    const QString input = m_sequencePreviewRight ? m_rightInput : m_leftInput;
    if (input.trimmed().isEmpty()) {
        return 0;
    }

    const ImageSequenceScanResult sequence = ImageSequenceUtils::parseImageSequencePattern(absolutePathFromWorkingDirectory(input));
    return sequence.ok ? std::max(m_startIndex, m_stopIndex) : 0;
}

int SliceController::sequencePreviewFrame() const
{
    return std::clamp(m_sequencePreviewFrame, sequencePreviewMinimum(), sequencePreviewMaximum());
}

void SliceController::setSequencePreviewFrame(int sequencePreviewFrame)
{
    const int value = std::clamp(sequencePreviewFrame, sequencePreviewMinimum(), sequencePreviewMaximum());
    if (m_sequencePreviewFrame == value) return;
    m_sequencePreviewFrame = value;
    Q_EMIT sequencePreviewChanged();
}

QString SliceController::sequencePreviewPath() const
{
    if (m_sequencePreviewRight && !m_stereo) {
        return QString();
    }

    const QString input = m_sequencePreviewRight ? m_rightInput : m_leftInput;
    if (input.trimmed().isEmpty()) {
        return QString();
    }

    const QString absoluteInput = absolutePathFromWorkingDirectory(input);
    if (m_inputType == QStringLiteral("Video")) {
        return QFileInfo::exists(absoluteInput) ? QDir::toNativeSeparators(absoluteInput) : QString();
    }

    const ImageSequenceScanResult sequence = ImageSequenceUtils::parseImageSequencePattern(absoluteInput);
    if (!sequence.ok) {
        return QFileInfo::exists(absoluteInput) ? QDir::toNativeSeparators(absoluteInput) : QString();
    }

    const QFileInfo inputInfo(absoluteInput);
    return QDir::toNativeSeparators(ImageSequenceUtils::buildFramePath(inputInfo.absolutePath(),
        sequence.prefix,
        sequence.digitCount,
        sequence.suffix,
        sequencePreviewFrame()));
}

QString SliceController::sequencePreviewStatus() const
{
    const QString side = m_sequencePreviewRight ? QStringLiteral("Right") : QStringLiteral("Left");
    if (m_sequencePreviewRight && !m_stereo) {
        return QStringLiteral("Right-eye input is disabled.");
    }

    const QString input = m_sequencePreviewRight ? m_rightInput : m_leftInput;
    if (input.trimmed().isEmpty()) {
        return m_inputType == QStringLiteral("Video")
            ? QStringLiteral("Choose a %1 input video.").arg(side.toLower())
            : QStringLiteral("Choose a %1 input image.").arg(side.toLower());
    }

    const QString absoluteInput = absolutePathFromWorkingDirectory(input);
    if (m_inputType == QStringLiteral("Video")) {
        const QFileInfo inputInfo(absoluteInput);
        if (!inputInfo.exists()) {
            return QStringLiteral("Video does not exist: %1").arg(input);
        }
        return QStringLiteral("%1 video frame %2 of %3-%4: %5")
            .arg(side)
            .arg(sequencePreviewFrame())
            .arg(sequencePreviewMinimum())
            .arg(sequencePreviewMaximum())
            .arg(inputInfo.fileName());
    }

    const ImageSequenceScanResult sequence = ImageSequenceUtils::parseImageSequencePattern(absoluteInput);
    if (!sequence.ok) {
        const QFileInfo inputInfo(absoluteInput);
        return inputInfo.exists()
            ? QStringLiteral("%1 single image: %2").arg(side, inputInfo.fileName())
            : sequence.message;
    }

    const int frame = sequencePreviewFrame();
    return QStringLiteral("%1 frame %2 of %3-%4")
        .arg(side,
            frameNumber(frame, sequence.digitCount),
            frameNumber(sequencePreviewMinimum(), sequence.digitCount),
            frameNumber(sequencePreviewMaximum(), sequence.digitCount));
}

void SliceController::sequencePreviewPrevious()
{
    setSequencePreviewFrame(sequencePreviewFrame() - 1);
}

void SliceController::sequencePreviewNext()
{
    setSequencePreviewFrame(sequencePreviewFrame() + 1);
}

void SliceController::forceRefreshImageSequenceStatus()
{
    if (m_inputType != QStringLiteral("Image sequence")) {
        refreshVideoMetadataStatus(true, true, m_stereo && !m_rightInput.trimmed().isEmpty());
        return;
    }
    refreshImageSequenceStatus(true, true, m_stereo && !m_rightInput.trimmed().isEmpty());
}

void SliceController::resetStartIndexToIndexedRange()
{
    if (m_hasIndexedRange) {
        setStartIndex(m_indexedStartIndex);
    }
}

void SliceController::resetStopIndexToIndexedRange()
{
    if (m_hasIndexedRange) {
        setStopIndex(m_indexedStopIndex);
    }
}

QString SliceController::outputSuffix() const
{
    return outputContainerSuffix();
}

QStringList SliceController::buildArguments(bool verifyOnly) const
{
    QStringList arguments;
    const QString configurationPath = configuredSlicePath(m_configuration, QStringLiteral("configs/default.json"));
    arguments << QStringLiteral("--node")
              << QStringLiteral("-input-type") << nodeInputTypeArgument(m_inputType)
              << QStringLiteral("--config") << cleanNativePath(configurationPath)
              << QStringLiteral("-left") << cleanNativePath(absolutePathFromWorkingDirectory(m_leftInput));

    if (m_stereo && !m_rightInput.isEmpty()) {
        arguments << QStringLiteral("-right") << cleanNativePath(absolutePathFromWorkingDirectory(m_rightInput));
    }
    if (m_upsideDown) {
        arguments << QStringLiteral("-upsidedown");
    }
    if (m_warping) {
        arguments << QStringLiteral("-warping");
    }
    if (m_blendMask) {
        arguments << QStringLiteral("-blend-mask");
    }

    // Image-specific options - only add when NOT using video input type

    arguments << QStringLiteral("-start") << QString::number(m_startIndex)
              << QStringLiteral("-stop") << QString::number(m_stopIndex)
              << QStringLiteral("-steps") << QString::number(m_steps)
              << QStringLiteral("-max-encoder-load-threads") << QString::number(m_maxEncoderThreads)
              << QStringLiteral("-capture-gpu-slots") << QString::number(m_captureGpuSlots);

    arguments << QStringLiteral("-mapping") << m_mappingMode
              << QStringLiteral("-surface-radius") << QString::number(m_surfaceRadius, 'f', 4);
    if (m_mappingMode == QStringLiteral("Dome")) {
        arguments << QStringLiteral("-surface-fov") << QString::number(m_surfaceFov, 'f', 4);
    }

    if (m_layerStereoMode != QStringLiteral("2D (mono)")) {
        arguments << QStringLiteral("-layer-stereo") << m_layerStereoMode;
    }
    if (m_layerAlpha != 100) {
        arguments << QStringLiteral("-layer-alpha") << QString::number(m_layerAlpha);
    }
    if (m_layerRoiEnabled) {
        arguments << QStringLiteral("-layer-roi")
                  << QStringLiteral("1")
                  << QString::number(m_layerRoiX, 'f', 6)
                  << QString::number(m_layerRoiY, 'f', 6)
                  << QString::number(m_layerRoiWidth, 'f', 6)
                  << QString::number(m_layerRoiHeight, 'f', 6);
    }
    if (m_mappingMode == QStringLiteral("Dome")) {
        if (!qFuzzyIsNull(m_layerYaw)) {
            arguments << QStringLiteral("-layer-rotate")
                      << QStringLiteral("0.0000")
                      << QString::number(m_layerYaw, 'f', 4)
                      << QStringLiteral("0.0000");
        }
    }
    else if (m_mappingMode == QStringLiteral("Sphere EQR") || m_mappingMode == QStringLiteral("Sphere EAC")) {
        if (!qFuzzyIsNull(m_layerPitch) || !qFuzzyIsNull(m_layerYaw) || !qFuzzyIsNull(m_layerRoll)) {
            arguments << QStringLiteral("-layer-rotate")
                      << QString::number(m_layerPitch, 'f', 4)
                      << QString::number(m_layerYaw, 'f', 4)
                      << QString::number(m_layerRoll, 'f', 4);
        }
    }
    if (m_mappingMode == QStringLiteral("Plane")) {
        arguments << QStringLiteral("-plane-orientation")
                  << QString::number(m_planeAzimuth, 'f', 4)
                  << QString::number(m_planeElevation, 'f', 4)
                  << QString::number(m_planeRoll, 'f', 4)
                  << QStringLiteral("-plane-position")
                  << QString::number(m_planeDistance, 'f', 4)
                  << QString::number(m_planeHorizontal, 'f', 4)
                  << QString::number(m_planeVertical, 'f', 4)
                  << QStringLiteral("-plane-size")
                  << QString::number(m_planeWidth, 'f', 4)
                  << QString::number(m_planeHeight, 'f', 4)
                  << QString::number(m_planeAspectRatio);
    }

    const QDir outputRoot(absolutePathFromWorkingDirectory(m_outputDirectory));
    for (int i = 0; i < m_outputCount; ++i) {
        if (!outputEnabled(i)) {
            continue;
        }
        const QString identifier = outputIdentifier(i);
        const QString outputFile = outputRoot.filePath(identifier + QLatin1Char('/') + m_outputName + outputSuffix());
        arguments << QStringLiteral("-out") << identifier << cleanNativePath(outputFile);
    }

    if (!m_runWithoutEncoding) {
        arguments << QStringLiteral("-codec") << m_codec;
        if (hasBitrateCodecName(m_codec)) {
            arguments << QStringLiteral("-pixrate") << QString::number(m_pixrate);
        }
        if (hasQualityCodecName(m_codec)) {
            arguments << QStringLiteral("-constantquality") << QString::number(constantQuality());
        }
    if (isMovieCodecName(m_codec)) {
        arguments << QStringLiteral("-framerate") << QString::number(m_frameRateNum) << QString::number(m_frameRateDen);
        arguments << QStringLiteral("-input-framerate-num") << QString::number(m_inputFrameRateNum) << QString::number(m_inputFrameRateDen);
    }
        if (hasPresetCodecName(m_codec)) {
            arguments << QStringLiteral("-preset") << preset();
        }
        if (isCrfCodecName(m_codec)) {
            arguments << QStringLiteral("-libx-tune") << m_libxTune;
        }
        if (isNvencCodecName(m_codec)) {
            arguments << QStringLiteral("-nvenc-tune") << m_nvencTune;
            if (m_nvencHardwareFrames) {
                arguments << QStringLiteral("-nvenc-hardware-frames");
            }
        }
        if (supportsEncodingBitDepthCodecName(m_codec)) {
            arguments << QStringLiteral("-encoding-bit-depth") << QString::number(m_encodingBitDepth);
        }
    }

    if (!m_runWithoutEncoding && !m_parameterFile.isEmpty()) {
        const QString parameterFile = configuredSlicePath(m_parameterFile, QStringLiteral("parameters"));
        arguments << QStringLiteral("-parameterFile") << cleanNativePath(parameterFile);
    }

    if (m_useOnlyIframes) {
        arguments << QStringLiteral("-use-only-iframes");
    }
    else {
        // Calculate GOP size in frames based on seconds and frame rate
        const int gopSizeFrames = std::max(1, static_cast<int>(std::round(m_gopSizeSeconds * m_frameRateDen / (double)m_frameRateNum)));
        arguments << QStringLiteral("-gop-size") << QString::number(gopSizeFrames);
    }
    if (verifyOnly) {
        arguments << QStringLiteral("-verify-only");
    }

    if (m_inputType != QStringLiteral("Video")) {
        arguments << QStringLiteral("-image-buffering-threads") << QString::number(m_imageBufferingThreadCount)
            << QStringLiteral("-image-size-warning-percent") << QString::number(m_imageSizeWarningPercent) 
            << QStringLiteral("-image-error-behavior") << m_imageErrorBehavior.toLower();
    }

    // Video decoding mode - only add when using video input type
    if (m_inputType == QStringLiteral("Video")) {
        arguments << QStringLiteral("--video-decoding-mode") << m_videoDecodingMode;
    }

    if (m_runWithoutEncoding) {
        arguments << QStringLiteral("-no-encode");
    }
    if (m_runWithoutReadback) {
        arguments << QStringLiteral("-no-readback");
    }

    return arguments;
}

QString SliceController::quoteArgument(const QString &argument)
{
    QString quoted = argument;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    if (quoted.contains(QLatin1Char(' ')) || quoted.contains(QLatin1Char('\t')) || quoted.contains(QLatin1Char('\\'))) {
        return QStringLiteral("\"") + quoted + QStringLiteral("\"");
    }
    return quoted;
}

void SliceController::verifySliceInputs()
{
    if (running()) {
        appendLog(QStringLiteral("C-Slice node is already running."));
        return;
    }
    if (m_leftInput.isEmpty()) {
        appendLog(QStringLiteral("Left/input media is required."));
        return;
    }
    if (m_inputType == QStringLiteral("Video")) {
        appendLog(QStringLiteral("Video input verification is skipped."));
        return;
    }

    const QStringList arguments = buildArguments(true);
    m_verifyingSlice = true;
    resetSliceProgress();
    startSliceRemainingTime();
    updateSliceProgress(0, 0, std::max(1, expectedSliceFrameCount()), m_startIndex, QStringLiteral("00:00:00"), QStringLiteral("Verifying image headers."));
    appendLog(QStringLiteral("Verifying: %1 %2")
        .arg(quoteArgument(QCoreApplication::applicationFilePath()), arguments.join(QLatin1Char(' '))));

    m_process.setProgram(QCoreApplication::applicationFilePath());
    m_process.setArguments(arguments);
    m_process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
}

QStringList SliceController::buildAudioMuxArguments(const QString &outputFile, int channelCount, int globalVolume, const QVariantList &channels, QString *error) const
{
    if (channelCount < 1) {
        if (error) *error = QStringLiteral("Audio channel layout must contain at least one channel.");
        return {};
    }
    if (channels.size() < channelCount) {
        if (error) *error = QStringLiteral("Audio channel data is incomplete.");
        return {};
    }

    const QString normalizedOutput = wavOutputPath(outputFile);
    if (normalizedOutput.isEmpty()) {
        if (error) *error = QStringLiteral("Audio output filename is required.");
        return {};
    }

    QStringList arguments;
    QStringList filterParts;
    QStringList mergeInputs;
    int inputCounter = 0;
    const double globalFactor = std::clamp(globalVolume, 0, 500) / 100.0;

    for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
        const QVariantMap channel = channels.at(channelIndex).toMap();
        const bool checked = channel.value(QStringLiteral("checked"), true).toBool();
        const int gain = std::clamp(channel.value(QStringLiteral("gain"), 100).toInt(), 0, 500);
        QString inputFile = normalizedPath(channel.value(QStringLiteral("file")).toString());

        if (checked) {
            if (inputFile.isEmpty()) {
                if (error) *error = QStringLiteral("Audio channel %1 requires an input file.").arg(channelIndex + 1);
                return {};
            }

            inputFile = cleanNativePath(absolutePathFromWorkingDirectory(inputFile));
            arguments << QStringLiteral("-i") << inputFile;

            const double channelVolume = globalFactor * (gain / 100.0);
            const QString volume = QLocale::c().toString(channelVolume, 'f', 2);
            filterParts << QStringLiteral("[%1:a]volume=%2:precision=fixed[a%3]")
                .arg(inputCounter++)
                .arg(volume)
                .arg(channelIndex);
        }
        else {
            filterParts << QStringLiteral("aevalsrc=0[a%1]").arg(channelIndex);
        }
        mergeInputs << QStringLiteral("[a%1]").arg(channelIndex);
    }

    QString filter = filterParts.join(QStringLiteral("; "));
    filter += QStringLiteral("; ");
    filter += mergeInputs.join(QString());
    filter += QStringLiteral("amerge=inputs=%1[aout]").arg(channelCount);

    arguments << QStringLiteral("-filter_complex") << filter
              << QStringLiteral("-map") << QStringLiteral("[aout]")
              << QStringLiteral("-y") << normalizedOutput;
    return arguments;
}

QString SliceController::buildAudioMuxCommandLine(const QString &outputFile, int channelCount, int globalVolume, const QVariantList &channels) const
{
    QString error;
    const QStringList arguments = buildAudioMuxArguments(outputFile, channelCount, globalVolume, channels, &error);
    if (!error.isEmpty()) {
        return error;
    }

    QStringList parts;
    parts << quoteArgument(ffmpegExecutable());
    for (const QString &argument : arguments) {
        parts << quoteArgument(argument);
    }
    return parts.join(QLatin1Char(' '));
}

QString SliceController::commandLinePreview() const
{
    QStringList parts;
    parts << quoteArgument(QFileInfo(QCoreApplication::applicationFilePath()).fileName());
    const QStringList arguments = buildArguments();
    for (const QString &argument : arguments) {
        parts << quoteArgument(argument);
    }
    return parts.join(QLatin1Char(' '));
}

void SliceController::muxAudio(const QString &outputFile, int channelCount, int globalVolume, const QVariantList &channels)
{
    if (audioMuxRunning()) {
        appendLog(QStringLiteral("Audio Muxer is already running."));
        return;
    }

    QString error;
    const QStringList arguments = buildAudioMuxArguments(outputFile, channelCount, globalVolume, channels, &error);
    if (!error.isEmpty()) {
        appendLog(error);
        return;
    }

    const QString outputPath = arguments.isEmpty() ? QString() : arguments.last();
    const QFileInfo outputInfo(outputPath);
    QDir outputDirectory(outputInfo.absolutePath());
    if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
        appendLog(QStringLiteral("Failed to create audio output directory: %1").arg(outputInfo.absolutePath()));
        return;
    }

    const QString executable = ffmpegExecutable();
    QStringList commandParts;
    commandParts << quoteArgument(executable);
    for (const QString &argument : arguments) {
        commandParts << quoteArgument(argument);
    }
    appendLog(QStringLiteral("Muxing audio: %1").arg(commandParts.join(QLatin1Char(' '))));

    m_audioMuxProcess.setProgram(executable);
    m_audioMuxProcess.setArguments(arguments);
    m_audioMuxProcess.setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_audioMuxProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_audioMuxProcess.start();
}

void SliceController::abortAudioMux()
{
    if (!audioMuxRunning()) {
        return;
    }
    appendLog(QStringLiteral("Aborting Audio Muxer process."));
    m_audioMuxProcess.terminate();
    QTimer::singleShot(3000, this, [this]() {
        if (audioMuxRunning()) {
            appendLog(QStringLiteral("Audio Muxer did not exit after terminate; killing process."));
            m_audioMuxProcess.kill();
        }
    });
}

void SliceController::launchSlice()
{
    if (running()) {
        appendLog(QStringLiteral("C-Slice node is already running."));
        return;
    }
    if (m_configuration.isEmpty()) {
        appendLog(QStringLiteral("Configuration is required."));
        return;
    }
    if (m_leftInput.isEmpty()) {
        appendLog(QStringLiteral("Left/input media is required."));
        return;
    }
    if (selectedOutputCount() == 0) {
        appendLog(QStringLiteral("At least one output must be selected."));
        return;
    }

    QDir outputRoot(absolutePathFromWorkingDirectory(m_outputDirectory));
    if (!outputRoot.exists() && !outputRoot.mkpath(QStringLiteral("."))) {
        appendLog(QStringLiteral("Failed to create output directory: %1").arg(m_outputDirectory));
        return;
    }
    for (int i = 0; i < m_outputCount; ++i) {
        if (!outputEnabled(i)) {
            continue;
        }
        const QString identifier = outputIdentifier(i);
        if (!outputRoot.mkpath(identifier)) {
            appendLog(QStringLiteral("Failed to create output directory for window %1.").arg(identifier));
            return;
        }
    }

    const QStringList arguments = buildArguments();
    m_verifyingSlice = false;
    m_slicePaused = false;
    Q_EMIT slicePausedChanged();
    resetSliceProgress();
    startSliceRemainingTime();
    updateSliceProgress(0, 0, std::max(1, expectedSliceFrameCount()), m_startIndex, QStringLiteral("00:00:00"), QStringLiteral("Launching node process."));
    appendLog(QStringLiteral("Launching: %1 %2")
        .arg(quoteArgument(QCoreApplication::applicationFilePath()), arguments.join(QLatin1Char(' '))));

    m_process.setProgram(QCoreApplication::applicationFilePath());
    m_process.setArguments(arguments);
    m_process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
}

void SliceController::pauseSlice()
{
    if (!running() || m_slicePaused) {
        return;
    }
    m_process.write("pause\n");
    m_process.waitForBytesWritten(100);
    m_slicePaused = true;
    appendLog(QStringLiteral("Pausing C-Slice node process."));
    updateSliceProgress(m_sliceLoadedFrames, m_sliceRenderedFrames, m_sliceTotalFrames, m_sliceCurrentFrame, m_sliceElapsedTime, QStringLiteral("Paused."));
    Q_EMIT slicePausedChanged();
}

void SliceController::resumeSlice()
{
    if (!running() || !m_slicePaused) {
        return;
    }
    m_process.write("resume\n");
    m_process.waitForBytesWritten(100);
    m_slicePaused = false;
    appendLog(QStringLiteral("Resuming C-Slice node process."));
    updateSliceProgress(m_sliceLoadedFrames, m_sliceRenderedFrames, m_sliceTotalFrames, m_sliceCurrentFrame, m_sliceElapsedTime, QStringLiteral("Resuming."));
    Q_EMIT slicePausedChanged();
}

void SliceController::abortSlice()
{
    if (!running()) {
        return;
    }
    appendLog(QStringLiteral("Aborting C-Slice node process."));
    m_process.terminate();
    QTimer::singleShot(3000, this, [this]() {
        if (running()) {
            appendLog(QStringLiteral("Node did not exit after terminate; killing process."));
            m_process.kill();
        }
    });
}

void SliceController::clearLog()
{
    if (m_logText.isEmpty()) {
        return;
    }
    m_logText.clear();
    Q_EMIT logTextChanged();
}

void SliceController::openOutputDirectory()
{
    if (m_outputDirectory.isEmpty()) {
        appendLog(QStringLiteral("Output directory is empty."));
        return;
    }

    const QFileInfo outputInfo(absolutePathFromWorkingDirectory(m_outputDirectory));
    if (!outputInfo.exists() || !outputInfo.isDir()) {
        appendLog(QStringLiteral("Output directory does not exist: %1").arg(m_outputDirectory));
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(outputInfo.absoluteFilePath()))) {
        appendLog(QStringLiteral("Failed to open output directory: %1").arg(m_outputDirectory));
    }
}

void SliceController::openFailedFile(const QString &path)
{
    const QString filePath = QDir::toNativeSeparators(path.trimmed());
    if (filePath.isEmpty()) {
        return;
    }

#ifdef Q_OS_WIN
    if (QFileInfo::exists(filePath)) {
        QProcess::startDetached(QStringLiteral("explorer.exe"), { QStringLiteral("/select,"), filePath });
        return;
    }
#endif

    const QFileInfo info(filePath);
    const QString directory = info.absolutePath();
    if (!directory.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    }
}

void SliceController::applyApplicationSettings()
{
    resetDialogLocationsFromSettings();
    const QString config = m_stereo ? SliceSettings::configuration3D() : SliceSettings::configuration2D();
    const QString fallbackConfig = m_stereo ? QStringLiteral("configs/nrkp_stereo_2026.json") : QStringLiteral("configs/nrkp_mono_2026.json");
    setConfiguration(configuredSlicePath(config, fallbackConfig));
    const QString outputDirectory = normalizedPath(SliceSettings::firstOutputDirectoryLocation());
    const QString outputName = m_outputName;
    setOutputDirectory(outputDirectory.isEmpty() ? defaultOutputDirectory() : outputDirectory);
    setOutputName(outputName);
    setMappingMode(normalizedMappingMode(SliceSettings::format()));
    setSurfaceRadius(SliceSettings::surfaceRadius());
    setSurfaceFov(SliceSettings::surfaceFov());
    setLayerPitch(SliceSettings::layerPitch());
    setLayerYaw(SliceSettings::layerYaw());
    setLayerRoll(SliceSettings::layerRoll());
    setPlaneAzimuth(SliceSettings::planeAzimuth());
    setPlaneElevation(SliceSettings::planeElevation());
    setPlaneRoll(SliceSettings::planeRoll());
    setPlaneDistance(SliceSettings::planeDistance());
    setPlaneHorizontal(SliceSettings::planeHorizontal());
    setPlaneVertical(SliceSettings::planeVertical());
    setPlaneWidth(SliceSettings::planeWidth());
    setPlaneHeight(SliceSettings::planeHeight());
    setMaxEncoderThreads(SliceSettings::maxEncoderThreads());
    setImageBufferingThreadCount(SliceSettings::imageBufferingThreadCount());
    setCaptureGpuSlots(SliceSettings::captureGpuSlots());
    setImageSizeWarningPercent(SliceSettings::imageSizeWarningPercent());
    setImageErrorBehavior(SliceSettings::imageErrorBehavior());
    setVideoDecodingMode(SliceSettings::videoDecodingMode());
    setRunWithoutEncoding(false);
    setRunWithoutReadback(false);
    setCodec(SliceSettings::codec());
    setFrameRateNum(SliceSettings::frameRateNum());
    setFrameRateDen(SliceSettings::frameRateDen());
    setSoftwarePreset(SliceSettings::softwarePreset());
    setNvencPreset(SliceSettings::nvencPreset());
    setLibxTune(SliceSettings::libxTune());
    setNvencTune(SliceSettings::nvencTune());
    setNvencHardwareFrames(SliceSettings::nvencHardwareFrames());
    setEncodingBitDepth(SliceSettings::encodingBitDepth());
    setCrf(SliceSettings::cRF());
    setCq(SliceSettings::cQ());
    setQscale(SliceSettings::qScale());
    setParameterFile(SliceSettings::paramFile().isEmpty() ? QString() : configuredSlicePath(SliceSettings::paramFile(), QStringLiteral("parameters")));
    setPreferNtscOutputFrameRates(SliceSettings::preferNtscOutputFrameRates());
    setPreferMatroska(SliceSettings::preferMatroska());
    setUseOnlyIframes(SliceSettings::useOnlyIframes());
    setGopSizeSeconds(SliceSettings::gopSizeSeconds());
    notifyAllSettingsChanged();
}

bool SliceController::useOnlyIframes() const { return m_useOnlyIframes; }
void SliceController::setUseOnlyIframes(bool useOnlyIframes)
{
    if (m_useOnlyIframes == useOnlyIframes) return;
    m_useOnlyIframes = useOnlyIframes;
    Q_EMIT useOnlyIframesChanged();
    notifyCommandChanged();
}

double SliceController::gopSizeSeconds() const { return m_gopSizeSeconds; }
void SliceController::setGopSizeSeconds(double gopSizeSeconds)
{
    const double value = std::max(0.1, gopSizeSeconds);
    if (qFuzzyCompare(m_gopSizeSeconds, value)) return;
    m_gopSizeSeconds = value;
    Q_EMIT gopSizeSecondsChanged();
    notifyCommandChanged();
}

bool SliceController::preferNtscOutputFrameRates() const { return m_preferNtscOutputFrameRates; }
void SliceController::setPreferNtscOutputFrameRates(bool preferNtscOutputFrameRates)
{
    if (m_preferNtscOutputFrameRates == preferNtscOutputFrameRates) return;
    m_preferNtscOutputFrameRates = preferNtscOutputFrameRates;
    Q_EMIT preferNtscOutputFrameRatesChanged();
}

void SliceController::loadApplicationSettings()
{
    SliceSettings::self()->load();
    applyApplicationSettings();
    appendLog(QStringLiteral("Loaded C-Slice application settings from disk."));
}

void SliceController::loadSystemApplicationSettings()
{
    applySystemSettingsFile();
    applyApplicationSettings();
    appendLog(QStringLiteral("Loaded C-Slice system application settings from %1.").arg(systemSettingsPath()));
}

QVariantMap SliceController::loadPresetValues(const QString &presetName) const
{
    return readSettingsFile(presetFilePath(presetName));
}

bool SliceController::savePresetValues(const QString &presetName, const QVariantMap &values)
{
    const bool saved = writeSettingsFile(presetFilePath(presetName), values);
    if (saved) {
        appendLog(QStringLiteral("Saved C-Slice preset '%1'.").arg(QFileInfo(presetName).completeBaseName()));
        Q_EMIT presetNamesChanged();
    }
    else {
        appendLog(QStringLiteral("Failed to save C-Slice preset '%1'.").arg(presetName));
    }
    return saved;
}

void SliceController::applyPresetValues(const QVariantMap &values)
{
    if (values.isEmpty()) {
        return;
    }

    if (values.contains(QStringLiteral("Configuration2D"))) setConfiguration(configuredSlicePath(values.value(QStringLiteral("Configuration2D")).toString(), QStringLiteral("configs/nrkp_mono_2026.json")));
    if (values.contains(QStringLiteral("FirstLeftInputLocation"))) setLeftInputDialogLocation(values.value(QStringLiteral("FirstLeftInputLocation")).toString(), false);
    if (values.contains(QStringLiteral("FirstRightInputLocation"))) setRightInputDialogLocation(values.value(QStringLiteral("FirstRightInputLocation")).toString(), false);
    if (values.contains(QStringLiteral("FirstOutputDirectoryLocation"))) setOutputDirectory(values.value(QStringLiteral("FirstOutputDirectoryLocation")).toString());
    if (values.contains(QStringLiteral("Format"))) setMappingMode(normalizedMappingMode(values.value(QStringLiteral("Format")).toString()));
    if (values.contains(QStringLiteral("SurfaceRadius"))) setSurfaceRadius(mapDouble(values, QStringLiteral("SurfaceRadius"), m_surfaceRadius));
    if (values.contains(QStringLiteral("SurfaceFov"))) setSurfaceFov(mapDouble(values, QStringLiteral("SurfaceFov"), m_surfaceFov));
    if (values.contains(QStringLiteral("LayerPitch"))) setLayerPitch(mapDouble(values, QStringLiteral("LayerPitch"), m_layerPitch));
    if (values.contains(QStringLiteral("LayerYaw"))) setLayerYaw(mapDouble(values, QStringLiteral("LayerYaw"), m_layerYaw));
    if (values.contains(QStringLiteral("LayerRoll"))) setLayerRoll(mapDouble(values, QStringLiteral("LayerRoll"), m_layerRoll));
    if (values.contains(QStringLiteral("PlaneAzimuth"))) setPlaneAzimuth(mapDouble(values, QStringLiteral("PlaneAzimuth"), m_planeAzimuth));
    if (values.contains(QStringLiteral("PlaneElevation"))) setPlaneElevation(mapDouble(values, QStringLiteral("PlaneElevation"), m_planeElevation));
    if (values.contains(QStringLiteral("PlaneRoll"))) setPlaneRoll(mapDouble(values, QStringLiteral("PlaneRoll"), m_planeRoll));
    if (values.contains(QStringLiteral("PlaneDistance"))) setPlaneDistance(mapDouble(values, QStringLiteral("PlaneDistance"), m_planeDistance));
    if (values.contains(QStringLiteral("PlaneHorizontal"))) setPlaneHorizontal(mapDouble(values, QStringLiteral("PlaneHorizontal"), m_planeHorizontal));
    if (values.contains(QStringLiteral("PlaneVertical"))) setPlaneVertical(mapDouble(values, QStringLiteral("PlaneVertical"), m_planeVertical));
    if (values.contains(QStringLiteral("PlaneWidth"))) setPlaneWidth(mapDouble(values, QStringLiteral("PlaneWidth"), m_planeWidth));
    if (values.contains(QStringLiteral("PlaneHeight"))) setPlaneHeight(mapDouble(values, QStringLiteral("PlaneHeight"), m_planeHeight));
    if (values.contains(QStringLiteral("MaxEncoderThreads"))) setMaxEncoderThreads(mapInt(values, QStringLiteral("MaxEncoderThreads"), m_maxEncoderThreads));
    if (values.contains(QStringLiteral("ImageBufferingThreadCount"))) setImageBufferingThreadCount(mapInt(values, QStringLiteral("ImageBufferingThreadCount"), m_imageBufferingThreadCount));
    if (values.contains(QStringLiteral("CaptureGpuSlots"))) setCaptureGpuSlots(mapInt(values, QStringLiteral("CaptureGpuSlots"), m_captureGpuSlots));
    if (values.contains(QStringLiteral("ImageSizeWarningPercent"))) setImageSizeWarningPercent(mapInt(values, QStringLiteral("ImageSizeWarningPercent"), m_imageSizeWarningPercent));
    if (values.contains(QStringLiteral("ImageErrorBehavior"))) setImageErrorBehavior(values.value(QStringLiteral("ImageErrorBehavior")).toString());
    if (values.contains(QStringLiteral("Codec"))) setCodec(values.value(QStringLiteral("Codec")).toString());
    if (values.contains(QStringLiteral("FrameRateNum"))) setFrameRateNum(mapInt(values, QStringLiteral("FrameRateNum"), m_frameRateNum));
    if (values.contains(QStringLiteral("FrameRateDen"))) setFrameRateDen(mapInt(values, QStringLiteral("FrameRateDen"), m_frameRateDen));
    if (values.contains(QStringLiteral("SoftwarePreset"))) setSoftwarePreset(values.value(QStringLiteral("SoftwarePreset")).toString());
    if (values.contains(QStringLiteral("NvencPreset"))) setNvencPreset(values.value(QStringLiteral("NvencPreset")).toString());
    if (values.contains(QStringLiteral("LibxTune"))) setLibxTune(values.value(QStringLiteral("LibxTune")).toString());
    if (values.contains(QStringLiteral("NvencTune"))) setNvencTune(values.value(QStringLiteral("NvencTune")).toString());
    if (values.contains(QStringLiteral("NvencHardwareFrames"))) setNvencHardwareFrames(mapBool(values, QStringLiteral("NvencHardwareFrames"), m_nvencHardwareFrames));
    if (values.contains(QStringLiteral("EncodingBitDepth"))) setEncodingBitDepth(mapInt(values, QStringLiteral("EncodingBitDepth"), m_encodingBitDepth));
    if (values.contains(QStringLiteral("CRF"))) setCrf(mapInt(values, QStringLiteral("CRF"), m_crf));
    if (values.contains(QStringLiteral("CQ"))) setCq(mapInt(values, QStringLiteral("CQ"), m_cq));
    if (values.contains(QStringLiteral("QScale"))) setQscale(mapInt(values, QStringLiteral("QScale"), m_qscale));
    if (values.contains(QStringLiteral("ParamFile"))) setParameterFile(values.value(QStringLiteral("ParamFile")).toString());
    if (values.contains(QStringLiteral("PreferNtscOutputFrameRates"))) setPreferNtscOutputFrameRates(mapBool(values, QStringLiteral("PreferNtscOutputFrameRates"), m_preferNtscOutputFrameRates));
    if (values.contains(QStringLiteral("PreferMatroska"))) setPreferMatroska(mapBool(values, QStringLiteral("PreferMatroska"), m_preferMatroska));
    if (values.contains(QStringLiteral("UseOnlyIframes"))) setUseOnlyIframes(mapBool(values, QStringLiteral("UseOnlyIframes"), m_useOnlyIframes));
    if (values.contains(QStringLiteral("GopSizeSeconds"))) setGopSizeSeconds(mapInt(values, QStringLiteral("GopSizeSeconds"), m_gopSizeSeconds));
    if (values.contains(QStringLiteral("VideoDecodingMode"))) setVideoDecodingMode(values.value(QStringLiteral("VideoDecodingMode")).toString());

    appendLog(QStringLiteral("Applied C-Slice preset values."));
}

void SliceController::saveApplicationSettings()
{
    if (m_stereo) {
        SliceSettings::setConfiguration3D(m_configuration);
    }
    else {
        SliceSettings::setConfiguration2D(m_configuration);
    }
    SliceSettings::setFormat(m_mappingMode);
    SliceSettings::setSurfaceRadius(m_surfaceRadius);
    SliceSettings::setSurfaceFov(m_surfaceFov);
    SliceSettings::setLayerPitch(m_layerPitch);
    SliceSettings::setLayerYaw(m_layerYaw);
    SliceSettings::setLayerRoll(m_layerRoll);
    SliceSettings::setPlaneAzimuth(m_planeAzimuth);
    SliceSettings::setPlaneElevation(m_planeElevation);
    SliceSettings::setPlaneRoll(m_planeRoll);
    SliceSettings::setPlaneDistance(m_planeDistance);
    SliceSettings::setPlaneHorizontal(m_planeHorizontal);
    SliceSettings::setPlaneVertical(m_planeVertical);
    SliceSettings::setPlaneWidth(m_planeWidth);
    SliceSettings::setPlaneHeight(m_planeHeight);
    SliceSettings::setMaxEncoderThreads(m_maxEncoderThreads);
    SliceSettings::setImageBufferingThreadCount(m_imageBufferingThreadCount);
    SliceSettings::setCaptureGpuSlots(m_captureGpuSlots);
    SliceSettings::setImageSizeWarningPercent(m_imageSizeWarningPercent);
    SliceSettings::setImageErrorBehavior(m_imageErrorBehavior);
    SliceSettings::setCodec(m_codec);
    SliceSettings::setFrameRateNum(m_frameRateNum);
    SliceSettings::setFrameRateDen(m_frameRateDen);
    SliceSettings::setSoftwarePreset(m_softwarePreset);
    SliceSettings::setNvencPreset(m_nvencPreset);
    SliceSettings::setLibxTune(m_libxTune);
    SliceSettings::setNvencTune(m_nvencTune);
    SliceSettings::setNvencHardwareFrames(m_nvencHardwareFrames);
    SliceSettings::setEncodingBitDepth(m_encodingBitDepth);
    SliceSettings::setCRF(m_crf);
    SliceSettings::setCQ(m_cq);
    SliceSettings::setQScale(m_qscale);
    SliceSettings::setParamFile(m_parameterFile);
    SliceSettings::setUseOnlyIframes(m_useOnlyIframes);
    // Convert seconds to frames based on current frame rate for storage
    SliceSettings::setGopSizeSeconds(m_gopSizeSeconds);
    if (!m_leftInput.isEmpty()) {
        SliceSettings::setFirstLeftInputLocation(parentDirectoryForFile(m_leftInput));
    }
    if (!m_rightInput.isEmpty()) {
        SliceSettings::setFirstRightInputLocation(parentDirectoryForFile(m_rightInput));
    }
    if (!m_outputDirectory.isEmpty()) {
        SliceSettings::setFirstOutputDirectoryLocation(m_outputDirectory);
    }
    SliceSettings::setPreferNtscOutputFrameRates(m_preferNtscOutputFrameRates);
    SliceSettings::setPreferMatroska(m_preferMatroska);
    if (m_inputType == QStringLiteral("Video")) {
        SliceSettings::setVideoDecodingMode(m_videoDecodingMode);
    }
    SliceSettings::self()->save();
    appendLog(QStringLiteral("Saved current C-Slice values as application settings."));
}

void SliceController::setOutputEnabled(int index, bool enabled)
{
    if (index < 0 || index >= m_outputEnabled.size() || m_outputEnabled[index] == enabled) {
        return;
    }
    m_outputEnabled[index] = enabled;
    Q_EMIT outputsChanged();
    notifyCommandChanged();
}

void SliceController::setAllOutputsEnabled(bool enabled)
{
    bool changed = false;
    for (bool &output : m_outputEnabled) {
        if (output != enabled) {
            output = enabled;
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    Q_EMIT outputsChanged();
    notifyCommandChanged();
}

void SliceController::appendLog(const QString &line)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, line]() {
            appendLog(line);
        }, Qt::QueuedConnection);
        return;
    }

    if (line.isEmpty()) {
        return;
    }
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QStringList lines = line.split(QLatin1Char('\n'));
    for (const QString &entry : lines) {
        if (!entry.trimmed().isEmpty()) {
            m_logText += QStringLiteral("[%1] %2\n").arg(timestamp, entry.trimmed());
        }
    }
    Q_EMIT logTextChanged();
}

void SliceController::handleProcessStdout(const QString &chunk)
{
    if (chunk.isEmpty()) {
        return;
    }

    m_processStdoutBuffer += chunk;
    int newlineIndex = -1;
    while ((newlineIndex = m_processStdoutBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = m_processStdoutBuffer.left(newlineIndex);
        m_processStdoutBuffer.remove(0, newlineIndex + 1);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        handleProcessOutputLine(line);
    }
}

void SliceController::handleProcessOutputLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (trimmed.startsWith(QStringLiteral("CriticalError "))) {
        const QString message = trimmed.mid(QStringLiteral("CriticalError ").size()).trimmed();
        setSliceError(message);
        return;
    }

    if (trimmed.startsWith(QStringLiteral("VerificationWarning "))) {
        const QString message = trimmed.mid(QStringLiteral("VerificationWarning ").size()).trimmed();
        appendLog(QStringLiteral("Warning: %1").arg(message));
        return;
    }

    if (trimmed == QStringLiteral("Paused")) {
        if (!m_slicePaused) {
            m_slicePaused = true;
            Q_EMIT slicePausedChanged();
        }
        updateSliceProgress(m_sliceLoadedFrames,
            m_sliceRenderedFrames,
            m_sliceTotalFrames,
            m_sliceCurrentFrame,
            m_sliceElapsedTime,
            QStringLiteral("Paused."));
        appendLog(trimmed);
        return;
    }

    if (trimmed == QStringLiteral("Resumed")) {
        if (m_slicePaused) {
            m_slicePaused = false;
            Q_EMIT slicePausedChanged();
        }
        updateSliceProgress(m_sliceLoadedFrames,
            m_sliceRenderedFrames,
            m_sliceTotalFrames,
            m_sliceCurrentFrame,
            m_sliceElapsedTime,
            QStringLiteral("Resuming."));
        appendLog(trimmed);
        return;
    }

    const QStringList parts = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!parts.isEmpty() && parts.first() == QStringLiteral("Verified")) {
        if (parts.size() >= 7) {
            const QString source = parts.at(1);
            bool okVerified = false;
            bool okTotal = false;
            bool okWidth = false;
            bool okHeight = false;
            const int verifiedFrames = parts.at(2).toInt(&okVerified);
            const int totalFrames = parts.at(3).toInt(&okTotal);
            const int width = parts.at(4).toInt(&okWidth);
            const int height = parts.at(5).toInt(&okHeight);
            if (okVerified && okTotal && okWidth && okHeight) {
                const int verificationTotalFrames = totalFrames * (m_stereo && !m_rightInput.isEmpty() ? 2 : 1);
                const int verificationVerifiedFrames = verifiedFrames + (source == QStringLiteral("right") ? totalFrames : 0);
                updateSliceProgress(verificationVerifiedFrames,
                    verificationVerifiedFrames,
                    verificationTotalFrames,
                    m_sliceCurrentFrame,
                    elapsedSecondsToTime(parts.at(6)),
                    QStringLiteral("Verifying %1 input: %2/%3 image headers (%4x%5).")
                        .arg(source)
                        .arg(verifiedFrames)
                        .arg(totalFrames)
                        .arg(width)
                        .arg(height));
                return;
            }
        }

        if (parts.size() >= 6) {
            const QString source = parts.at(1);
            bool okVerified = false;
            bool okTotal = false;
            bool okWidth = false;
            bool okHeight = false;
            const int verifiedFrames = parts.at(2).toInt(&okVerified);
            const int totalFrames = parts.at(3).toInt(&okTotal);
            const int width = parts.at(4).toInt(&okWidth);
            const int height = parts.at(5).toInt(&okHeight);
            if (okVerified && okTotal && okWidth && okHeight) {
                const int verificationTotalFrames = totalFrames * (m_stereo && !m_rightInput.isEmpty() ? 2 : 1);
                const int verificationVerifiedFrames = verifiedFrames + (source == QStringLiteral("right") ? totalFrames : 0);
                updateSliceProgress(verificationVerifiedFrames,
                    verificationVerifiedFrames,
                    verificationTotalFrames,
                    m_sliceCurrentFrame,
                    m_sliceElapsedTime,
                    QStringLiteral("Verifying %1 input: %2/%3 image headers (%4x%5).")
                        .arg(source)
                        .arg(verifiedFrames)
                        .arg(totalFrames)
                        .arg(width)
                        .arg(height));
                return;
            }
        }

        if (parts.size() >= 5) {
            bool okVerified = false;
            bool okTotal = false;
            bool okWidth = false;
            bool okHeight = false;
            const int verifiedFrames = parts.at(1).toInt(&okVerified);
            const int totalFrames = parts.at(2).toInt(&okTotal);
            const int width = parts.at(3).toInt(&okWidth);
            const int height = parts.at(4).toInt(&okHeight);
            if (okVerified && okTotal && okWidth && okHeight) {
                updateSliceProgress(verifiedFrames,
                    verifiedFrames,
                    totalFrames,
                    m_sliceCurrentFrame,
                    m_sliceElapsedTime,
                    QStringLiteral("Verified %1/%2 image headers (%3x%4).")
                        .arg(verifiedFrames)
                        .arg(totalFrames)
                        .arg(width)
                        .arg(height));
                return;
            }
        }
    }

    if (trimmed == QStringLiteral("Verification complete")) {
        updateSliceProgress(m_sliceTotalFrames,
            m_sliceTotalFrames,
            m_sliceTotalFrames,
            m_sliceCurrentFrame,
            m_sliceElapsedTime,
            QStringLiteral("Verified."));
        appendLog(trimmed);
        return;
    }

    if (!parts.isEmpty() && parts.first() == QStringLiteral("Progress")) {
        if (parts.size() >= 7) {
            bool okPercent = false;
            bool okFrame = false;
            bool okLoaded = false;
            bool okRendered = false;
            bool okTotal = false;
            const int percent = parts.at(1).toInt(&okPercent);
            const int currentFrame = parts.at(2).toInt(&okFrame);
            const int loadedFrames = parts.at(3).toInt(&okLoaded);
            const int renderedFrames = parts.at(4).toInt(&okRendered);
            const int totalFrames = parts.at(5).toInt(&okTotal);
            if (okPercent && okFrame && okLoaded && okRendered && okTotal) {
                Q_UNUSED(percent)
                updateSliceProgress(loadedFrames,
                    renderedFrames,
                    totalFrames,
                    currentFrame,
                    elapsedSecondsToTime(parts.at(6)),
                    m_slicePaused ? QStringLiteral("Paused.") : QStringLiteral("Running..."));
                return;
            }
        }

        if (parts.size() >= 6) {
            bool okPercent = false;
            bool okFrame = false;
            const int percent = parts.at(1).toInt(&okPercent);
            const int currentFrame = parts.at(2).toInt(&okFrame);
            if (okPercent && okFrame) {
                const int totalFrames = std::max(1, m_sliceTotalFrames);
                const int renderedFrames = std::clamp((percent * totalFrames) / 100, 0, totalFrames);
                updateSliceProgress(renderedFrames,
                    renderedFrames,
                    totalFrames,
                    currentFrame,
                    elapsedHmsToTime(parts.at(3), parts.at(4), parts.at(5)),
                    m_slicePaused ? QStringLiteral("Paused.") : QStringLiteral("Processed frame %1.").arg(currentFrame));
                return;
            }
        }

        if (parts.size() >= 4) {
            bool okPercent = false;
            bool okFrame = false;
            const int percent = parts.at(1).toInt(&okPercent);
            const int currentFrame = parts.at(2).toInt(&okFrame);
            if (okPercent && okFrame) {
                const int totalFrames = std::max(1, m_sliceTotalFrames);
                const int renderedFrames = std::clamp((percent * totalFrames) / 100, 0, totalFrames);
                updateSliceProgress(renderedFrames,
                    renderedFrames,
                    totalFrames,
                    currentFrame,
                    elapsedSecondsToTime(parts.at(3)),
                    m_slicePaused ? QStringLiteral("Paused.") : QStringLiteral("Processed frame %1.").arg(currentFrame));
                return;
            }
        }
    }

    if (trimmed == QStringLiteral("Done") || trimmed == QStringLiteral("All done")) {
        updateSliceProgress(m_sliceLoadedFrames,
            m_sliceRenderedFrames,
            m_sliceTotalFrames,
            m_sliceCurrentFrame,
            m_sliceElapsedTime,
            QStringLiteral("Finalizing output."));
        appendLog(trimmed);
        return;
    }

    if (trimmed.startsWith(QStringLiteral("Exporting took"))) {
        updateSliceProgress(m_sliceLoadedFrames,
            m_sliceRenderedFrames,
            m_sliceTotalFrames,
            m_sliceCurrentFrame,
            m_sliceElapsedTime,
            QStringLiteral("Completed."));
    }
    appendLog(trimmed);
}

void SliceController::resetSliceProgress()
{
    resetSliceRemainingTime();
    m_processStdoutBuffer.clear();
    m_processStderrBuffer.clear();
    m_lastSliceError.clear();
    m_sliceCriticalErrors.clear();
    m_sliceFailedFiles.clear();
    m_sliceProgressAction = m_verifyingSlice ? QStringLiteral("Verified") : (m_runWithoutEncoding ? QStringLiteral("Rendered") : QStringLiteral("Encoded"));
    updateSliceProgress(0, 0, 0, m_startIndex, QString(), QString());
}

void SliceController::startSliceRemainingTime()
{
    const int totalFrames = std::max(1, expectedSliceFrameCount());
    setSliceRemainingTime(QStringLiteral("Calculating…"));
    if (!m_sliceEstimateThread) {
        return;
    }
    m_sliceEstimateThread->startEstimate(totalFrames, m_verifyingSlice ? SliceEstimateThread::Mode::Verify : SliceEstimateThread::Mode::Render);
}

void SliceController::resetSliceRemainingTime()
{
    setSliceRemainingTime(QString());
    if (m_sliceEstimateThread) {
        m_sliceEstimateThread->resetEstimate();
    }
}

void SliceController::setSliceRemainingTime(const QString &remainingTime)
{
    if (m_sliceRemainingTime == remainingTime) {
        return;
    }
    m_sliceRemainingTime = remainingTime;
    Q_EMIT sliceRemainingTimeChanged();
}

void SliceController::setSliceError(const QString &message)
{
    const QString value = message.trimmed();
    if (value.isEmpty()) {
        return;
    }

    m_lastSliceError = value;
    const bool isNewError = !m_sliceCriticalErrors.contains(value);
    if (!m_sliceCriticalErrors.contains(value)) {
        m_sliceCriticalErrors << value;
    }
    const QString failedPath = failedFilePathFromMessage(value);
    if (!failedPath.isEmpty()) {
        addFailedFile(failedPath, value);
    }
    updateSliceProgress(m_sliceLoadedFrames,
        m_sliceRenderedFrames,
        m_sliceTotalFrames,
        m_sliceCurrentFrame,
        m_sliceElapsedTime,
        QStringLiteral("Critical error"));
    if (isNewError) {
        appendLog(value);
    }
    Q_EMIT sliceProgressChanged();
}

void SliceController::addFailedFile(const QString &path, const QString &message)
{
    const QString cleanPath = QDir::toNativeSeparators(path.trimmed());
    if (cleanPath.isEmpty()) {
        return;
    }
    for (const QVariant &entry : m_sliceFailedFiles) {
        if (entry.toMap().value(QStringLiteral("path")).toString() == cleanPath) {
            return;
        }
    }

    QVariantMap entry;
    entry.insert(QStringLiteral("path"), cleanPath);
    entry.insert(QStringLiteral("name"), QFileInfo(cleanPath).fileName().isEmpty() ? cleanPath : QFileInfo(cleanPath).fileName());
    entry.insert(QStringLiteral("message"), message);
    m_sliceFailedFiles << entry;
}

QString SliceController::failedFilePathFromMessage(const QString &message)
{
    const int closeIndex = message.lastIndexOf(QLatin1Char(')'));
    const int openIndex = closeIndex > 0 ? message.lastIndexOf(QLatin1Char('('), closeIndex) : -1;
    if (openIndex < 0 || closeIndex <= openIndex + 1) {
        return QString();
    }

    const QString candidate = message.mid(openIndex + 1, closeIndex - openIndex - 1).trimmed();
    if (candidate.contains(QLatin1Char('/')) || candidate.contains(QLatin1Char('\\')) || candidate.contains(QLatin1Char(':'))) {
        return candidate;
    }
    return QString();
}

void SliceController::updateSliceProgress(int loadedFrames, int renderedFrames, int totalFrames, int currentFrame, const QString &elapsedTime, const QString &status)
{
    totalFrames = std::max(0, totalFrames);
    loadedFrames = std::clamp(loadedFrames, 0, std::max(loadedFrames, totalFrames));
    renderedFrames = std::clamp(renderedFrames, 0, std::max(renderedFrames, totalFrames));
    const int loadedProgress = totalFrames > 0 ? std::clamp((loadedFrames * 100) / totalFrames, 0, 100) : 0;
    const int renderedProgress = totalFrames > 0 ? std::clamp((renderedFrames * 100) / totalFrames, 0, 100) : 0;

    if (m_sliceLoadedFrames == loadedFrames &&
        m_sliceRenderedFrames == renderedFrames &&
        m_sliceTotalFrames == totalFrames &&
        m_sliceCurrentFrame == currentFrame &&
        m_sliceElapsedTime == elapsedTime &&
        m_sliceProgressStatus == status &&
        m_sliceLoadedProgress == loadedProgress &&
        m_sliceRenderedProgress == renderedProgress) {
        return;
    }

    m_sliceLoadedFrames = loadedFrames;
    m_sliceRenderedFrames = renderedFrames;
    m_sliceTotalFrames = totalFrames;
    m_sliceCurrentFrame = currentFrame;
    m_sliceElapsedTime = elapsedTime;
    m_sliceProgressStatus = status;
    m_sliceLoadedProgress = loadedProgress;
    m_sliceRenderedProgress = renderedProgress;
    Q_EMIT sliceProgressChanged();
    if (totalFrames > 0 && running()) {
        Q_UNUSED(currentFrame)
        if (m_sliceEstimateThread) {
            m_sliceEstimateThread->updateProgress(renderedFrames, totalFrames);
        }
    }
}

int SliceController::expectedSliceFrameCount() const
{
    return std::max(1, ImageSequenceUtils::expectedFrameCount(m_startIndex, m_stopIndex, m_steps));
}

void SliceController::updateOutputCountFromConfiguration()
{
    const QStringList outputNames = outputNamesInConfiguration(configuredSlicePath(m_configuration, QStringLiteral("configs/default.json")));
    if (!outputNames.isEmpty()) {
        setOutputNames(outputNames);
        appendLog(QStringLiteral("Configuration contains %1 selectable SGCT window output(s).").arg(outputNames.size()));
    }
}

void SliceController::setOutputCountAndNamesSilent(int count, const QStringList &names)
{
    // Set output count and names without triggering re-index or validation.
    // This is used when restoring job settings from disk to avoid the side-effect
    // of updateOutputCountFromConfiguration which calls setOutputNames, which resets
    // all enabled flags based on previous state (often leaving none selected).
    if (names.isEmpty()) {
        return;
    }

    QList<bool> enabled;
    enabled.reserve(names.size());
    for (int i = 0; i < names.size(); ++i) {
        // Preserve existing enabled state if the index existed, otherwise enable by default
        enabled << (i < m_outputEnabled.size() ? m_outputEnabled.at(i) : true);
    }

    const bool countChanged = m_outputCount != names.size();
    if (!countChanged && m_outputNames == names && m_outputEnabled == enabled) {
        return;
    }

    m_outputCount = names.size();
    m_outputNames = names;
    m_outputEnabled = enabled;
    if (countChanged) {
        Q_EMIT outputCountChanged();
    }
    Q_EMIT outputsChanged();
}

void SliceController::setOutputNames(const QStringList &names)
{
    if (names.isEmpty()) {
        return;
    }

    QList<bool> enabled;
    enabled.reserve(names.size());
    for (int i = 0; i < names.size(); ++i) {
        enabled << (i < m_outputEnabled.size() ? m_outputEnabled.at(i) : true);
    }

    const bool countChanged = m_outputCount != names.size();
    if (!countChanged && m_outputNames == names && m_outputEnabled == enabled) {
        return;
    }

    m_outputCount = names.size();
    m_outputNames = names;
    m_outputEnabled = enabled;
    if (countChanged) {
        Q_EMIT outputCountChanged();
    }
    Q_EMIT outputsChanged();
    notifyCommandChanged();
}

bool SliceController::outputEnabled(int index) const
{
    return index >= 0 && index < m_outputEnabled.size() && m_outputEnabled.at(index);
}

QString SliceController::outputIdentifier(int index) const
{
    if (index >= 0 && index < m_outputNames.size()) {
        const QString name = m_outputNames.at(index).trimmed();
        if (!name.isEmpty()) {
            return name;
        }
    }
    return QStringLiteral("Output %1").arg(index);
}

void SliceController::setLeftInputDialogLocation(const QString &path, bool persist)
{
    const QString value = directoryForDialogLocation(path, standardPicturesPath());
    if (m_leftInputDialogLocation == value) {
        return;
    }
    m_leftInputDialogLocation = value;
    if (persist) {
        SliceSettings::setLastLeftFileDialogLocation(value);
        SliceSettings::self()->save();
    }
    Q_EMIT leftInputDialogLocationChanged();
}

void SliceController::setRightInputDialogLocation(const QString &path, bool persist)
{
    const QString value = directoryForDialogLocation(path, standardPicturesPath());
    if (m_rightInputDialogLocation == value) {
        return;
    }
    m_rightInputDialogLocation = value;
    if (persist) {
        SliceSettings::setLastRightFileDialogLocation(value);
        SliceSettings::self()->save();
    }
    Q_EMIT rightInputDialogLocationChanged();
}

void SliceController::setOutputDirectoryDialogLocation(const QString &path, bool persist)
{
    const QString value = directoryForDialogLocation(path, standardMoviesPath());
    if (m_outputDirectoryDialogLocation == value) {
        return;
    }
    m_outputDirectoryDialogLocation = value;
    if (persist) {
        SliceSettings::setLastOutputPathDialogLocation(value);
        SliceSettings::self()->save();
    }
    Q_EMIT outputDirectoryDialogLocationChanged();
}

void SliceController::resetDialogLocationsFromSettings()
{
    const QString leftPath = SliceSettings::lastLeftFileDialogLocation().isEmpty() ? SliceSettings::firstLeftInputLocation() : SliceSettings::lastLeftFileDialogLocation();
    const QString rightPath = SliceSettings::lastRightFileDialogLocation().isEmpty() ? SliceSettings::firstRightInputLocation() : SliceSettings::lastRightFileDialogLocation();
    const QString outputPath = SliceSettings::lastOutputPathDialogLocation().isEmpty() ? SliceSettings::firstOutputDirectoryLocation() : SliceSettings::lastOutputPathDialogLocation();
    const QString leftLocation = directoryForDialogLocation(leftPath, standardPicturesPath());
    const QString rightLocation = directoryForDialogLocation(rightPath, standardPicturesPath());
    const QString outputLocation = directoryForDialogLocation(outputPath, standardMoviesPath());

    if (m_leftInputDialogLocation != leftLocation) {
        m_leftInputDialogLocation = leftLocation;
        Q_EMIT leftInputDialogLocationChanged();
    }
    if (m_rightInputDialogLocation != rightLocation) {
        m_rightInputDialogLocation = rightLocation;
        Q_EMIT rightInputDialogLocationChanged();
    }
    if (m_outputDirectoryDialogLocation != outputLocation) {
        m_outputDirectoryDialogLocation = outputLocation;
        Q_EMIT outputDirectoryDialogLocationChanged();
    }
}

void SliceController::setSequenceStatus(const QString &status)
{
    if (m_sequenceStatus == status) {
        return;
    }
    m_sequenceStatus = status;
    Q_EMIT sequenceStatusChanged();
}

void SliceController::setSequenceIndexing(bool sequenceIndexing)
{
    if (m_sequenceIndexing == sequenceIndexing) {
        return;
    }
    m_sequenceIndexing = sequenceIndexing;
    Q_EMIT sequenceIndexingChanged();
}

void SliceController::refreshImageSequenceStatus(bool adoptDetectedRange, bool scanLeft, bool scanRight)
{
    if (m_inputType != QStringLiteral("Image sequence")) {
        ++m_sequenceIndexRequestId;
        setSequenceIndexing(false);
        setSequenceStatus(QStringLiteral("Video input selected. Frame range is read from video metadata when launching."));
        return;
    }
    scanRight = scanRight && m_stereo && !m_rightInput.trimmed().isEmpty();
    if (m_leftInput.trimmed().isEmpty()) {
        ++m_sequenceIndexRequestId;
        setSequenceIndexing(false);
        setSequenceStatus(QString());
        return;
    }

    const QString leftPath = absolutePathFromWorkingDirectory(m_leftInput);
    const QString rightPath = (m_stereo && !m_rightInput.trimmed().isEmpty())
        ? absolutePathFromWorkingDirectory(m_rightInput)
        : QString();
    const int requestId = ++m_sequenceIndexRequestId;
    setSequenceIndexing(true);
    setSequenceStatus(QStringLiteral("Indexing images…"));
    if (m_imageSequenceIndexThread) {
        m_imageSequenceIndexThread->requestIndex(requestId, adoptDetectedRange, scanLeft, scanRight, leftPath, rightPath);
    }
}

void SliceController::refreshVideoMetadataStatus(bool adoptDetectedRange, bool probeLeft, bool probeRight)
{
    probeLeft = true;
    probeRight = probeRight && m_stereo && !m_rightInput.trimmed().isEmpty();
    if (m_leftInput.trimmed().isEmpty()) {
        ++m_sequenceIndexRequestId;
        setSequenceIndexing(false);
        setSequenceStatus(QString());
        return;
    }

    const QString leftPath = absolutePathFromWorkingDirectory(m_leftInput);
    const QString rightPath = (m_stereo && !m_rightInput.trimmed().isEmpty())
        ? absolutePathFromWorkingDirectory(m_rightInput)
        : QString();
    const int requestId = ++m_sequenceIndexRequestId;
    setSequenceIndexing(true);
    setSequenceStatus(QStringLiteral("Reading video metadata…"));
    if (m_videoMetadataThread) {
        m_videoMetadataThread->requestMetadata(requestId, adoptDetectedRange, probeLeft, probeRight, leftPath, rightPath);
    }
}

void SliceController::applyImageSequenceStatus(int requestId,
    bool adoptDetectedRange,
    bool scanLeft,
    bool scanRight,
    const QString &leftPath,
    const QString &rightPath,
    const QVariantMap &leftSequence,
    const QVariantMap &rightSequence)
{
    if (requestId != m_sequenceIndexRequestId) {
        return;
    }
    setSequenceIndexing(false);
    if (scanLeft) {
        m_lastIndexedLeftPath = leftPath;
        m_lastIndexedLeftSequence = leftSequence;
    }
    if (scanRight) {
        m_lastIndexedRightPath = rightPath;
        m_lastIndexedRightSequence = rightSequence;
    }
    updateSequenceStatusFromScans(adoptDetectedRange,
        scanLeft ? leftPath : m_lastIndexedLeftPath,
        rightPath.isEmpty() ? QString() : (scanRight ? rightPath : m_lastIndexedRightPath),
        scanLeft ? leftSequence : m_lastIndexedLeftSequence,
        scanRight ? rightSequence : m_lastIndexedRightSequence);
    Q_EMIT sequencePreviewChanged();
}

void SliceController::updateSequenceStatusFromScans(bool adoptDetectedRange,
    const QString &leftPath,
    const QString &rightPath,
    const QVariantMap &leftSequenceValues,
    const QVariantMap &rightSequenceValues)
{
    const ImageSequenceScanResult leftSequence = sequenceResultFromMap(leftSequenceValues);
    if (!leftSequence.ok) {
        if (m_hasIndexedRange) {
            m_hasIndexedRange = false;
            Q_EMIT indexedRangeChanged();
        }
        if (adoptDetectedRange && m_startIndex != 0) {
            m_startIndex = 0;
            Q_EMIT startIndexChanged();
        }
        if (adoptDetectedRange && m_stopIndex != 0) {
            m_stopIndex = 0;
            Q_EMIT stopIndexChanged();
        }

        const QFileInfo leftInfo(leftPath);
        setSequenceStatus(leftInfo.exists()
            ? QStringLiteral("Single image: %1").arg(leftInfo.fileName())
            : leftSequence.message);
        return;
    }

    const bool detectedRangeChanged = !m_hasIndexedRange ||
        m_indexedStartIndex != leftSequence.firstIndex ||
        m_indexedStopIndex != leftSequence.lastIndex;
    m_hasIndexedRange = true;
    m_indexedStartIndex = leftSequence.firstIndex;
    m_indexedStopIndex = leftSequence.lastIndex;
    if (detectedRangeChanged) {
        Q_EMIT indexedRangeChanged();
    }

    if (adoptDetectedRange) {
        if (m_startIndex != leftSequence.firstIndex) {
            m_startIndex = leftSequence.firstIndex;
            Q_EMIT startIndexChanged();
        }
        if (m_stopIndex != leftSequence.lastIndex) {
            m_stopIndex = leftSequence.lastIndex;
            Q_EMIT stopIndexChanged();
        }
    }

    const int requestedFrames = ImageSequenceUtils::expectedFrameCount(m_startIndex, m_stopIndex, m_steps);
    QStringList details;
    details << QStringLiteral("%1 frames %2-%3")
        .arg(leftSequence.count)
        .arg(frameNumber(leftSequence.firstIndex, leftSequence.digitCount))
        .arg(frameNumber(leftSequence.lastIndex, leftSequence.digitCount));
    details << QStringLiteral("pattern %1").arg(sequencePattern(leftSequence));
    details << QStringLiteral("selected %1").arg(frameNumber(leftSequence.selectedIndex, leftSequence.digitCount));
    details << QStringLiteral("range %1-%2 step %3: %4 output frames")
        .arg(m_startIndex)
        .arg(m_stopIndex)
        .arg(std::max(1, m_steps))
        .arg(requestedFrames);
    if (leftSequence.missingFrames && !leftSequence.missingFrameIndices.isEmpty()) {
        // Format missing frame indices with compact range notation (e.g., "5, 6, 12, 23-27")
        QStringList formattedGaps;
        int i = 0;
        while (i < leftSequence.missingFrameIndices.size()) {
            const int start = leftSequence.missingFrameIndices[i];
            int end = start;
            while (i + 1 < leftSequence.missingFrameIndices.size() &&
                   leftSequence.missingFrameIndices[i + 1] == end + 1) {
                ++i;
                end = leftSequence.missingFrameIndices[i];
            }
            if (start == end) {
                formattedGaps << frameNumber(start, leftSequence.digitCount);
            }
            else {
                formattedGaps << QStringLiteral("%1-%2").arg(frameNumber(start, leftSequence.digitCount), frameNumber(end, leftSequence.digitCount));
            }
            ++i;
        }
        details << QStringLiteral("gaps detected: %1").arg(formattedGaps.join(QLatin1String(", ")));
    }

    if (!rightPath.isEmpty()) {
        const ImageSequenceScanResult rightSequence = sequenceResultFromMap(rightSequenceValues);
        if (!rightSequence.ok) {
            details << QStringLiteral("right input is not a matching sequence");
        }
        else if (rightSequence.firstIndex != leftSequence.firstIndex ||
            rightSequence.lastIndex != leftSequence.lastIndex ||
            rightSequence.count != leftSequence.count) {
            details << QStringLiteral("right range %1-%2 (%3 frames)")
                .arg(frameNumber(rightSequence.firstIndex, rightSequence.digitCount))
                .arg(frameNumber(rightSequence.lastIndex, rightSequence.digitCount))
                .arg(rightSequence.count);
            if (rightSequence.missingFrames && !rightSequence.missingFrameIndices.isEmpty()) {
                // Format missing frame indices with compact range notation (e.g., "5, 6, 12, 23-27")
                QStringList formattedGaps;
                int i = 0;
                while (i < rightSequence.missingFrameIndices.size()) {
                    const int start = rightSequence.missingFrameIndices[i];
                    int end = start;
                    while (i + 1 < rightSequence.missingFrameIndices.size() &&
                           rightSequence.missingFrameIndices[i + 1] == end + 1) {
                        ++i;
                        end = rightSequence.missingFrameIndices[i];
                    }
                    if (start == end) {
                        formattedGaps << frameNumber(start, rightSequence.digitCount);
                    }
                    else {
                        formattedGaps << QStringLiteral("%1-%2").arg(frameNumber(start, rightSequence.digitCount), frameNumber(end, rightSequence.digitCount));
                    }
                    ++i;
                }
                details << QStringLiteral("right gaps detected: %1").arg(formattedGaps.join(QLatin1String(", ")));
            }
        }
        else {
            details << QStringLiteral("right sequence matches");
        }
    }

    setSequenceStatus(details.join(QStringLiteral(" | ")));
}

void SliceController::notifyCommandChanged()
{
    Q_EMIT commandLinePreviewChanged();
}

void SliceController::notifyAllSettingsChanged()
{
    Q_EMIT configurationChanged();
    Q_EMIT inputTypeChanged();
    Q_EMIT mappingModeChanged();
    Q_EMIT surfaceRadiusChanged();
    Q_EMIT surfaceFovChanged();
    Q_EMIT layerSettingsChanged();
    Q_EMIT maxEncoderThreadsChanged();
    Q_EMIT imageBufferingThreadCountChanged();
    Q_EMIT captureGpuSlotsChanged();
    Q_EMIT imageSizeWarningPercentChanged();
    Q_EMIT runWithoutEncodingChanged();
    Q_EMIT slicePausedChanged();
    Q_EMIT imageErrorBehaviorChanged();
    if (m_inputType == QStringLiteral("Video")) {
        Q_EMIT videoDecodingModeChanged();
    }
    Q_EMIT codecChanged();
    Q_EMIT frameRateNumChanged();
    Q_EMIT frameRateDenChanged();
    Q_EMIT inputFrameRateNumChanged();
    Q_EMIT inputFrameRateDenChanged();
    Q_EMIT softwarePresetChanged();
    Q_EMIT nvencPresetChanged();
    Q_EMIT presetChanged();
    Q_EMIT libxTuneChanged();
    Q_EMIT nvencTuneChanged();
    Q_EMIT nvencHardwareFramesChanged();
    Q_EMIT encodingBitDepthChanged();
    Q_EMIT constantQualityChanged();
    Q_EMIT crfChanged();
    Q_EMIT cqChanged();
    Q_EMIT qscaleChanged();
    Q_EMIT useOnlyIframesChanged();
    Q_EMIT gopSizeSecondsChanged();
    Q_EMIT parameterFileChanged();
    Q_EMIT outputContainerSuffixChanged();
    Q_EMIT outputContainerSuffixesChanged();
    Q_EMIT leftInputDialogLocationChanged();
    Q_EMIT rightInputDialogLocationChanged();
    Q_EMIT outputDirectoryDialogLocationChanged();
    notifyCommandChanged();
    Q_EMIT sequencePreviewChanged();
}

// ============= Queue Implementation (mirroring C-Stitch) =============

QString SliceController::generateJobName() const
{
    // Use the output name as the primary identifier
    QString jobStem = m_outputName.trimmed().isEmpty() ? QStringLiteral("slice") : m_outputName;

    const int frameCount = expectedSliceFrameCount();
    return QString(QStringLiteral("%1 - %2 - %3 frames"))
        .arg(jobStem)
        .arg(m_inputType)
        .arg(frameCount);
}

void SliceController::connectToCJobServer()
{
    if (m_cjobSocket && m_cjobSocket->isOpen()) {
        return;  // Already connected
    }

    m_cjobSocket = new QLocalSocket(this);
    connect(m_cjobSocket, &QLocalSocket::connected, this, [this]() {
        qCInfo(cjobClient) << "Connected to C-Job server";
        sendRegisterInstance();
        for (const JobInfo &job : m_queue) {
            submitToCJob(job.jobId);
        }
    });
    connect(m_cjobSocket, &QLocalSocket::disconnected, this, [this]() {
        qCInfo(cjobClient) << "Disconnected from C-Job server";
        m_cjobBuffer.clear();
    });
    connect(m_cjobSocket, &QLocalSocket::readyRead, this, &SliceController::onCJobReadyRead);

    m_cjobSocket->connectToServer(QStringLiteral("C-Job"));
}

void SliceController::disconnectFromCJobServer()
{
    if (m_cjobSocket) {
        m_cjobSocket->close();
        delete m_cjobSocket;
        m_cjobSocket = nullptr;
    }

    // Clear current job ID when disconnecting
    m_currentCJobJobId.clear();
}

void SliceController::sendRegisterInstance()
{
    if (!m_cjobSocket || !m_cjobSocket->isOpen()) {
        return;
    }

    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("register_instance");
    json[QStringLiteral("instance_id")] = QString(QCoreApplication::applicationName() + QLatin1Char('-') + QString::number(QRandomGenerator::global()->generate()));
    sendJsonToCJob(json);
}

void SliceController::sendJsonToCJob(const QJsonObject &json)
{
    if (!m_cjobSocket || !m_cjobSocket->isOpen()) {
        return;
    }

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append('\n');
    m_cjobSocket->write(data);
}

void SliceController::onCJobReadyRead()
{
    while (m_cjobSocket && m_cjobSocket->bytesAvailable() > 0) {
        QByteArray data = m_cjobSocket->readAll();
        m_cjobBuffer += QString::fromUtf8(data);

        int newlinePos;
        while ((newlinePos = m_cjobBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
            QString message = m_cjobBuffer.left(newlinePos);
            m_cjobBuffer = m_cjobBuffer.mid(newlinePos + 1);

            if (!message.trimmed().isEmpty()) {
                processCJobMessage(message);
            }
        }
    }
}

void SliceController::processCJobMessage(const QString &message)
{
    qCDebug(cjobClient) << "Received from C-Job:" << message;

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        qCWarning(cjobClient) << "Invalid JSON message";
        return;
    }

    QJsonObject obj = doc.object();
    QString type = obj[QStringLiteral("type")].toString();

    if (type == QLatin1String("launch_job")) {
        QString jobId = obj[QStringLiteral("job_id")].toString();
        if (!jobId.isEmpty()) {
            qCInfo(cjobClient) << "Received launch command from C-Job for job:" << jobId;

            // Find the matching job in the queue by ID and set runningJobIndex so UI updates correctly
            int runningIndex = -1;
            for (int i = 0; i < m_queue.size(); ++i) {
                if (m_queue[i].jobId == jobId) {
                    runningIndex = i;
                    break;
                }
            }

            // Store the current job ID and set the running index so UI updates correctly
            m_currentCJobJobId = jobId;
            m_runningJobIndex = runningIndex;
            Q_EMIT runningJobIndexChanged();

            // Retrieve stored parameters by job ID and apply them before launching
            JobInfo storedParams = retrieveStoredJob(jobId);
            if (!storedParams.jobId.isEmpty()) {
                qCInfo(cjobClient) << "Retrieved stored params for job:" << storedParams.jobName;
                const JobInfo originalJobSettings = captureCurrentJobSettings();

                // Temporarily store current settings to restore after launch
                QString origInputType = m_inputType;
                QString origLeftInput = m_leftInput;
                QString origRightInput = m_rightInput;
                QString origOutputDirectory = m_outputDirectory;
                QString origOutputName = m_outputName;
                bool origStereo = m_stereo;
                bool origUpsideDown = m_upsideDown;
                bool origWarping = m_warping;
                bool origBlendMask = m_blendMask;
                int origStartIndex = m_startIndex;
                int origStopIndex = m_stopIndex;
                int origSteps = m_steps;
                QString origMappingMode = m_mappingMode;
                double origSurfaceRadius = m_surfaceRadius;
                double origSurfaceFov = m_surfaceFov;
                QString origLayerStereoMode = m_layerStereoMode;
                int origLayerAlpha = m_layerAlpha;
                bool origLayerRoiEnabled = m_layerRoiEnabled;
                double origLayerRoiX = m_layerRoiX;
                double origLayerRoiY = m_layerRoiY;
                double origLayerRoiWidth = m_layerRoiWidth;
                double origLayerRoiHeight = m_layerRoiHeight;
                double origLayerPitch = m_layerPitch;
                double origLayerYaw = m_layerYaw;
                double origLayerRoll = m_layerRoll;
                double origPlaneAzimuth = m_planeAzimuth;
                double origPlaneElevation = m_planeElevation;
                double origPlaneRoll = m_planeRoll;
                double origPlaneDistance = m_planeDistance;
                double origPlaneHorizontal = m_planeHorizontal;
                double origPlaneVertical = m_planeVertical;
                double origPlaneWidth = m_planeWidth;
                double origPlaneHeight = m_planeHeight;
                int origPlaneAspectRatio = m_planeAspectRatio;
                QString origCodec = m_codec;
                QString origPreset = preset();
                QString origSoftwarePreset = m_softwarePreset;
                QString origNvencPreset = m_nvencPreset;
                QString origLibxTune = m_libxTune;
                QString origNvencTune = m_nvencTune;
                bool origNvencHardwareFrames = m_nvencHardwareFrames;
                int origEncodingBitDepth = m_encodingBitDepth;
                int origPixrate = m_pixrate;
                int origConstantQuality = m_constantQuality;
                int origCrf = m_crf;
                int origCq = m_cq;
                int origQscale = m_qscale;
                int origFrameRateNum = m_frameRateNum;
                int origFrameRateDen = m_frameRateDen;
                QString origParameterFile = m_parameterFile;
                bool origUseOnlyIframes = m_useOnlyIframes;
                double origGopSizeSeconds = m_gopSizeSeconds;
                bool origPreferMatroska = m_preferMatroska;
                QString origOutputContainerSuffix = outputContainerSuffix();
                int origMaxEncoderThreads = m_maxEncoderThreads;
                int origImageBufferingThreadCount = m_imageBufferingThreadCount;
                int origCaptureGpuSlots = m_captureGpuSlots;

                // Apply stored job parameters
                setInputType(storedParams.inputType);
                setLeftInput(storedParams.leftInput);
                setRightInput(storedParams.rightInput);
                setOutputDirectory(storedParams.outputDirectory);
                setOutputName(storedParams.outputName);
                setStereo(storedParams.stereo);
                setUpsideDown(storedParams.upsideDown);
                setWarping(storedParams.warping);
                setBlendMask(storedParams.blendMask);
                setStartIndex(storedParams.startIndex);
                setStopIndex(storedParams.stopIndex);
                setSteps(storedParams.steps);
                setMappingMode(storedParams.mappingMode);
                setSurfaceRadius(storedParams.surfaceRadius);
                setSurfaceFov(storedParams.surfaceFov);
                setLayerStereoMode(storedParams.layerStereoMode);
                setLayerAlpha(storedParams.layerAlpha);
                setLayerRoiEnabled(storedParams.layerRoiEnabled);
                setLayerRoiX(storedParams.layerRoiX);
                setLayerRoiY(storedParams.layerRoiY);
                setLayerRoiWidth(storedParams.layerRoiWidth);
                setLayerRoiHeight(storedParams.layerRoiHeight);
                setLayerPitch(storedParams.layerPitch);
                setLayerYaw(storedParams.layerYaw);
                setLayerRoll(storedParams.layerRoll);
                setPlaneAzimuth(storedParams.planeAzimuth);
                setPlaneElevation(storedParams.planeElevation);
                setPlaneRoll(storedParams.planeRoll);
                setPlaneDistance(storedParams.planeDistance);
                setPlaneHorizontal(storedParams.planeHorizontal);
                setPlaneVertical(storedParams.planeVertical);
                setPlaneWidth(storedParams.planeWidth);
                setPlaneHeight(storedParams.planeHeight);
                setPlaneAspectRatio(storedParams.planeAspectRatio);
                setCodec(storedParams.codec);
                setPreset(storedParams.preset);
                setSoftwarePreset(storedParams.softwarePreset);
                setNvencPreset(storedParams.nvencPreset);
                setLibxTune(storedParams.libxTune);
                setNvencTune(storedParams.nvencTune);
                setNvencHardwareFrames(storedParams.nvencHardwareFrames);
                setEncodingBitDepth(storedParams.encodingBitDepth);
                setPixrate(storedParams.pixrate);
                setConstantQuality(storedParams.constantQuality);
                setCrf(storedParams.crf);
                setCq(storedParams.cq);
                setQscale(storedParams.qscale);
                setFrameRateNum(storedParams.frameRateNum);
                setFrameRateDen(storedParams.frameRateDen);
                setParameterFile(storedParams.parameterFile);
                setUseOnlyIframes(storedParams.useOnlyIframes);
                setGopSizeSeconds(storedParams.gopSizeSeconds);
                setPreferMatroska(storedParams.preferMatroska);
                setOutputContainerSuffix(storedParams.outputContainerSuffix);
                setMaxEncoderThreads(storedParams.maxEncoderThreads);
                setImageBufferingThreadCount(storedParams.imageBufferingThreadCount);
                setCaptureGpuSlots(storedParams.captureGpuSlots);

                // Apply settings omitted by the legacy per-property block before launch.
                applyJobSettings(storedParams);

                // Launch the slice with stored job parameters
                launchSlice();

                // Restore original settings after launch (queued to avoid signal conflicts)
                QMetaObject::invokeMethod(this, [this, origLeftInput, origRightInput, origOutputDirectory, origOutputName,
                    origInputType, origStereo, origUpsideDown, origWarping, origBlendMask,
                    origStartIndex, origStopIndex, origSteps, origMappingMode,
                    origSurfaceRadius, origSurfaceFov, origLayerStereoMode, origLayerAlpha,
                    origLayerRoiEnabled, origLayerRoiX, origLayerRoiY, origLayerRoiWidth, origLayerRoiHeight,
                    origLayerPitch, origLayerYaw, origLayerRoll,
                    origPlaneAzimuth, origPlaneElevation, origPlaneRoll,
                    origPlaneDistance, origPlaneHorizontal, origPlaneVertical,
                    origPlaneWidth, origPlaneHeight, origPlaneAspectRatio,
                    origCodec, origPreset, origSoftwarePreset, origNvencPreset,
                    origLibxTune, origNvencTune, origNvencHardwareFrames,
                    origEncodingBitDepth, origPixrate, origConstantQuality,
                    origCrf, origCq, origQscale,
                    origFrameRateNum, origFrameRateDen, origParameterFile,
                    origUseOnlyIframes, origGopSizeSeconds, origPreferMatroska,
                    origOutputContainerSuffix, origMaxEncoderThreads,
                    origImageBufferingThreadCount, origCaptureGpuSlots, originalJobSettings]() {
                    setInputType(origInputType);
                    setLeftInput(origLeftInput);
                    setRightInput(origRightInput);
                    setOutputDirectory(origOutputDirectory);
                    setOutputName(origOutputName);
                    setStereo(origStereo);
                    setUpsideDown(origUpsideDown);
                    setWarping(origWarping);
                    setBlendMask(origBlendMask);
                    setStartIndex(origStartIndex);
                    setStopIndex(origStopIndex);
                    setSteps(origSteps);
                    setMappingMode(origMappingMode);
                    setSurfaceRadius(origSurfaceRadius);
                    setSurfaceFov(origSurfaceFov);
                    setLayerStereoMode(origLayerStereoMode);
                    setLayerAlpha(origLayerAlpha);
                    setLayerRoiEnabled(origLayerRoiEnabled);
                    setLayerRoiX(origLayerRoiX);
                    setLayerRoiY(origLayerRoiY);
                    setLayerRoiWidth(origLayerRoiWidth);
                    setLayerRoiHeight(origLayerRoiHeight);
                    setLayerPitch(origLayerPitch);
                    setLayerYaw(origLayerYaw);
                    setLayerRoll(origLayerRoll);
                    setPlaneAzimuth(origPlaneAzimuth);
                    setPlaneElevation(origPlaneElevation);
                    setPlaneRoll(origPlaneRoll);
                    setPlaneDistance(origPlaneDistance);
                    setPlaneHorizontal(origPlaneHorizontal);
                    setPlaneVertical(origPlaneVertical);
                    setPlaneWidth(origPlaneWidth);
                    setPlaneHeight(origPlaneHeight);
                    setPlaneAspectRatio(origPlaneAspectRatio);
                    setCodec(origCodec);
                    setPreset(origPreset);
                    setSoftwarePreset(origSoftwarePreset);
                    setNvencPreset(origNvencPreset);
                    setLibxTune(origLibxTune);
                    setNvencTune(origNvencTune);
                    setNvencHardwareFrames(origNvencHardwareFrames);
                    setEncodingBitDepth(origEncodingBitDepth);
                    setPixrate(origPixrate);
                    setConstantQuality(origConstantQuality);
                    setCrf(origCrf);
                    setCq(origCq);
                    setQscale(origQscale);
                    setFrameRateNum(origFrameRateNum);
                    setFrameRateDen(origFrameRateDen);
                    setParameterFile(origParameterFile);
                    setUseOnlyIframes(origUseOnlyIframes);
                    setGopSizeSeconds(origGopSizeSeconds);
                    setPreferMatroska(origPreferMatroska);
                    setOutputContainerSuffix(origOutputContainerSuffix);
                    setMaxEncoderThreads(origMaxEncoderThreads);
                    setImageBufferingThreadCount(origImageBufferingThreadCount);
                    setCaptureGpuSlots(origCaptureGpuSlots);
                    applyJobSettings(originalJobSettings);
                }, Qt::QueuedConnection);
            } else {
                qCWarning(cjobClient) << "No stored parameters found for job:" << jobId << "- launching with current settings";
                launchSlice();
            }
        }
    } else if (type == QLatin1String("job_complete")) {
        QString jobId = obj[QStringLiteral("job_id")].toString();
        qCInfo(cjobClient) << "Job completed in C-Job:" << jobId;

        // Clean up stored job parameters after completion
        m_storedJobs.remove(jobId);
    }
}

bool SliceController::isCJobEnabled() const
{
    return m_cjobEnabled;
}

void SliceController::setCJobEnabled(bool enabled)
{
    if (m_cjobEnabled == enabled) {
        return;
    }

    m_cjobEnabled = enabled;

    if (m_cjobEnabled) {
        connectToCJobServer();
    } else {
        disconnectFromCJobServer();
        // Clear current job ID when disabling C-Job
        m_currentCJobJobId.clear();
    }

    Q_EMIT cjobEnabledChanged();
}

bool SliceController::isCJobConnected() const
{
    return m_cjobSocket && m_cjobSocket->isOpen();
}

QStringList SliceController::internalQueuedJobs() const
{
    QStringList jobs;
    for (const JobInfo &info : m_queue) {
        jobs << info.jobName;
    }
    return jobs;
}

SliceController::JobInfo SliceController::captureCurrentJobSettings() const
{
    JobInfo info;
    info.configuration = m_configuration;
    info.inputType = m_inputType;
    info.leftInput = m_leftInput;
    info.rightInput = m_rightInput;
    info.outputDirectory = m_outputDirectory;
    info.outputName = m_outputName;
    info.stereo = m_stereo;
    info.upsideDown = m_upsideDown;
    info.warping = m_warping;
    info.blendMask = m_blendMask;
    info.startIndex = m_startIndex;
    info.stopIndex = m_stopIndex;
    info.steps = m_steps;
    info.outputCount = m_outputCount;
    info.outputNames = m_outputNames;
    info.outputEnabled = m_outputEnabled;
    info.maxEncoderThreads = m_maxEncoderThreads;
    info.imageBufferingThreadCount = m_imageBufferingThreadCount;
    info.captureGpuSlots = m_captureGpuSlots;
    info.imageSizeWarningPercent = m_imageSizeWarningPercent;
    info.runWithoutEncoding = m_runWithoutEncoding;
    info.runWithoutReadback = m_runWithoutReadback;
    info.mappingMode = m_mappingMode;
    info.surfaceRadius = m_surfaceRadius;
    info.surfaceFov = m_surfaceFov;
    info.layerStereoMode = m_layerStereoMode;
    info.layerAlpha = m_layerAlpha;
    info.layerRoiEnabled = m_layerRoiEnabled;
    info.layerRoiX = m_layerRoiX;
    info.layerRoiY = m_layerRoiY;
    info.layerRoiWidth = m_layerRoiWidth;
    info.layerRoiHeight = m_layerRoiHeight;
    info.layerPitch = m_layerPitch;
    info.layerYaw = m_layerYaw;
    info.layerRoll = m_layerRoll;
    info.planeAzimuth = m_planeAzimuth;
    info.planeElevation = m_planeElevation;
    info.planeRoll = m_planeRoll;
    info.planeDistance = m_planeDistance;
    info.planeHorizontal = m_planeHorizontal;
    info.planeVertical = m_planeVertical;
    info.planeWidth = m_planeWidth;
    info.planeHeight = m_planeHeight;
    info.planeAspectRatio = m_planeAspectRatio;
    info.codec = m_codec;
    info.preset = preset();
    info.softwarePreset = m_softwarePreset;
    info.nvencPreset = m_nvencPreset;
    info.libxTune = m_libxTune;
    info.nvencTune = m_nvencTune;
    info.nvencHardwareFrames = m_nvencHardwareFrames;
    info.encodingBitDepth = m_encodingBitDepth;
    info.pixrate = m_pixrate;
    info.constantQuality = m_constantQuality;
    info.crf = m_crf;
    info.cq = m_cq;
    info.qscale = m_qscale;
    info.frameRateNum = m_frameRateNum;
    info.frameRateDen = m_frameRateDen;
    info.inputFrameRateNum = m_inputFrameRateNum;
    info.inputFrameRateDen = m_inputFrameRateDen;
    info.parameterFile = m_parameterFile;
    info.useOnlyIframes = m_useOnlyIframes;
    info.gopSizeSeconds = m_gopSizeSeconds;
    info.preferNtscOutputFrameRates = m_preferNtscOutputFrameRates;
    info.preferMatroska = m_preferMatroska;
    info.outputContainerSuffix = outputContainerSuffix();
    info.imageErrorBehavior = m_imageErrorBehavior;
    info.videoDecodingMode = m_videoDecodingMode;
    return info;
}

void SliceController::applyJobSettings(const JobInfo &info)
{
    // Use silent setter for configuration to avoid triggering updateOutputCountFromConfiguration,
    // which would reset output enabled flags based on the config file rather than the saved job state.
    const QString value = relativePathIfInWorkingDirectory(normalizedPath(info.configuration));
    if (m_configuration != value) {
        m_configuration = value;
        Q_EMIT configurationChanged();
        updateConfigFeatureOptions();
        notifyCommandChanged();
    }
    setInputType(info.inputType);
    setLeftInput(info.leftInput);
    setRightInput(info.rightInput);
    setOutputDirectory(info.outputDirectory);
    setOutputName(info.outputName);
    setStereo(info.stereo);
    setUpsideDown(info.upsideDown);
    setWarping(info.warping);
    setBlendMask(info.blendMask);
    setStartIndex(info.startIndex);
    setStopIndex(info.stopIndex);
    setSteps(info.steps);

    // Use silent setter to preserve saved output enabled state from the job.
    // This avoids the side-effect of updateOutputCountFromConfiguration which resets
    // all enabled flags based on the config file (often leaving none selected).
    if (!info.outputNames.isEmpty()) {
        setOutputCountAndNamesSilent(info.outputCount, info.outputNames);
        // Apply the saved output enabled flags from the job settings.
        // This ensures the "Outputs" selection is restored when loading a saved job.
        if (!info.outputEnabled.isEmpty()) {
            m_outputEnabled = info.outputEnabled;
            Q_EMIT outputsChanged();
        }
    } else {
        m_outputCount = info.outputCount;
        m_outputEnabled = info.outputEnabled;
        Q_EMIT outputCountChanged();
        Q_EMIT outputsChanged();
    }
    setMaxEncoderThreads(info.maxEncoderThreads);
    setImageBufferingThreadCount(info.imageBufferingThreadCount);
    setCaptureGpuSlots(info.captureGpuSlots);
    setImageSizeWarningPercent(info.imageSizeWarningPercent);
    setRunWithoutEncoding(info.runWithoutEncoding);
    setRunWithoutReadback(info.runWithoutReadback);
    setMappingMode(info.mappingMode);
    setSurfaceRadius(info.surfaceRadius);
    setSurfaceFov(info.surfaceFov);
    setLayerStereoMode(info.layerStereoMode);
    setLayerAlpha(info.layerAlpha);
    setLayerRoiEnabled(info.layerRoiEnabled);
    setLayerRoiX(info.layerRoiX);
    setLayerRoiY(info.layerRoiY);
    setLayerRoiWidth(info.layerRoiWidth);
    setLayerRoiHeight(info.layerRoiHeight);
    setLayerPitch(info.layerPitch);
    setLayerYaw(info.layerYaw);
    setLayerRoll(info.layerRoll);
    setPlaneAzimuth(info.planeAzimuth);
    setPlaneElevation(info.planeElevation);
    setPlaneRoll(info.planeRoll);
    setPlaneDistance(info.planeDistance);
    setPlaneHorizontal(info.planeHorizontal);
    setPlaneVertical(info.planeVertical);
    setPlaneWidth(info.planeWidth);
    setPlaneHeight(info.planeHeight);
    setPlaneAspectRatio(info.planeAspectRatio);
    setCodec(info.codec);
    setPreset(info.preset);
    setSoftwarePreset(info.softwarePreset);
    setNvencPreset(info.nvencPreset);
    setLibxTune(info.libxTune);
    setNvencTune(info.nvencTune);
    setNvencHardwareFrames(info.nvencHardwareFrames);
    setEncodingBitDepth(info.encodingBitDepth);
    setPixrate(info.pixrate);
    setConstantQuality(info.constantQuality);
    setCrf(info.crf);
    setCq(info.cq);
    setQscale(info.qscale);
    setFrameRateNum(info.frameRateNum);
    setFrameRateDen(info.frameRateDen);
    setInputFrameRateNum(info.inputFrameRateNum);
    setInputFrameRateDen(info.inputFrameRateDen);
    setParameterFile(info.parameterFile);
    setUseOnlyIframes(info.useOnlyIframes);
    setGopSizeSeconds(info.gopSizeSeconds);
    setPreferNtscOutputFrameRates(info.preferNtscOutputFrameRates);
    setPreferMatroska(info.preferMatroska);
    setOutputContainerSuffix(info.outputContainerSuffix);
    setImageErrorBehavior(info.imageErrorBehavior);
    setVideoDecodingMode(info.videoDecodingMode);
}

bool SliceController::canPersistInternalQueue(QString *error) const
{
    if (running() || m_internalQueueRunning || m_cjobEnabled || isEditingQueuedJob()) {
        if (error) *error = QStringLiteral("The queue cannot be changed while running, editing, or using C-Job.");
        return false;
    }
    return true;
}

QJsonObject SliceController::jobInfoToJson(const JobInfo &info)
{
    QJsonObject json;
    // Serialize output names and enabled flags as parallel arrays for JSON compatibility
    QJsonArray outputNamesArray;
    for (const QString &name : info.outputNames) {
        outputNamesArray.append(name);
    }
    QJsonArray outputEnabledArray;
    for (bool enabled : info.outputEnabled) {
        outputEnabledArray.append(enabled);
    }

    json[QStringLiteral("jobId")] = info.jobId;
    json[QStringLiteral("jobName")] = info.jobName;
    json[QStringLiteral("settings")] = QJsonObject::fromVariantMap({
        { QStringLiteral("configuration"), info.configuration }, { QStringLiteral("inputType"), info.inputType },
        { QStringLiteral("leftInput"), info.leftInput }, { QStringLiteral("rightInput"), info.rightInput },
        { QStringLiteral("outputDirectory"), info.outputDirectory }, { QStringLiteral("outputName"), info.outputName },
        { QStringLiteral("stereo"), info.stereo }, { QStringLiteral("upsideDown"), info.upsideDown },
        { QStringLiteral("warping"), info.warping }, { QStringLiteral("blendMask"), info.blendMask },
        { QStringLiteral("startIndex"), info.startIndex }, { QStringLiteral("stopIndex"), info.stopIndex },
        { QStringLiteral("steps"), info.steps }, { QStringLiteral("outputCount"), info.outputCount },
        { QStringLiteral("outputNames"), outputNamesArray },
        { QStringLiteral("outputEnabled"), outputEnabledArray },
        { QStringLiteral("codec"), info.codec }, { QStringLiteral("preset"), info.preset },
        { QStringLiteral("softwarePreset"), info.softwarePreset }, { QStringLiteral("nvencPreset"), info.nvencPreset },
        { QStringLiteral("libxTune"), info.libxTune }, { QStringLiteral("nvencTune"), info.nvencTune },
        { QStringLiteral("nvencHardwareFrames"), info.nvencHardwareFrames },
        { QStringLiteral("encodingBitDepth"), info.encodingBitDepth }, { QStringLiteral("pixrate"), info.pixrate },
        { QStringLiteral("constantQuality"), info.constantQuality }, { QStringLiteral("crf"), info.crf },
        { QStringLiteral("cq"), info.cq }, { QStringLiteral("qscale"), info.qscale },
        { QStringLiteral("frameRateNum"), info.frameRateNum }, { QStringLiteral("frameRateDen"), info.frameRateDen },
        { QStringLiteral("inputFrameRateNum"), info.inputFrameRateNum }, { QStringLiteral("inputFrameRateDen"), info.inputFrameRateDen },
        { QStringLiteral("parameterFile"), info.parameterFile },
        { QStringLiteral("useOnlyIframes"), info.useOnlyIframes }, { QStringLiteral("gopSizeSeconds"), info.gopSizeSeconds },
        { QStringLiteral("preferNtscOutputFrameRates"), info.preferNtscOutputFrameRates },
        { QStringLiteral("preferMatroska"), info.preferMatroska },
        { QStringLiteral("mappingMode"), info.mappingMode },
        { QStringLiteral("surfaceRadius"), info.surfaceRadius }, { QStringLiteral("surfaceFov"), info.surfaceFov },
        { QStringLiteral("layerStereoMode"), info.layerStereoMode }, { QStringLiteral("layerAlpha"), info.layerAlpha },
        { QStringLiteral("layerRoiEnabled"), info.layerRoiEnabled }, { QStringLiteral("layerRoiX"), info.layerRoiX },
        { QStringLiteral("layerRoiY"), info.layerRoiY }, { QStringLiteral("layerRoiWidth"), info.layerRoiWidth },
        { QStringLiteral("layerRoiHeight"), info.layerRoiHeight },
        { QStringLiteral("layerPitch"), info.layerPitch }, { QStringLiteral("layerYaw"), info.layerYaw },
        { QStringLiteral("layerRoll"), info.layerRoll },
        { QStringLiteral("planeAzimuth"), info.planeAzimuth }, { QStringLiteral("planeElevation"), info.planeElevation },
        { QStringLiteral("planeRoll"), info.planeRoll }, { QStringLiteral("planeDistance"), info.planeDistance },
        { QStringLiteral("planeHorizontal"), info.planeHorizontal }, { QStringLiteral("planeVertical"), info.planeVertical },
        { QStringLiteral("planeWidth"), info.planeWidth }, { QStringLiteral("planeHeight"), info.planeHeight },
        { QStringLiteral("planeAspectRatio"), info.planeAspectRatio },
        { QStringLiteral("maxEncoderThreads"), info.maxEncoderThreads },
        { QStringLiteral("imageBufferingThreadCount"), info.imageBufferingThreadCount },
        { QStringLiteral("captureGpuSlots"), info.captureGpuSlots },
        { QStringLiteral("imageSizeWarningPercent"), info.imageSizeWarningPercent },
        { QStringLiteral("runWithoutEncoding"), info.runWithoutEncoding },
        { QStringLiteral("runWithoutReadback"), info.runWithoutReadback },
        { QStringLiteral("outputContainerSuffix"), info.outputContainerSuffix },
        { QStringLiteral("imageErrorBehavior"), info.imageErrorBehavior },
        { QStringLiteral("videoDecodingMode"), info.videoDecodingMode }
    });
    return json;
}

bool SliceController::jobInfoFromJson(const QJsonObject &json, JobInfo *info, QString *error)
{
    const QJsonObject settings = json.value(QStringLiteral("settings")).toObject();
    const QString jobId = json.value(QStringLiteral("jobId")).toString();
    if (jobId.isEmpty() || settings.isEmpty()) { if (error) *error = QStringLiteral("A queue job is missing required fields."); return false; }
    JobInfo result;
    result.jobId = jobId; result.jobName = json.value(QStringLiteral("jobName")).toString(); result.status = QStringLiteral("Pending");
    
    // Basic file/path settings
    result.configuration = settings.value(QStringLiteral("configuration")).toString();
    result.inputType = settings.value(QStringLiteral("inputType")).toString(result.inputType);
    result.leftInput = settings.value(QStringLiteral("leftInput")).toString();
    result.rightInput = settings.value(QStringLiteral("rightInput")).toString();
    result.outputDirectory = settings.value(QStringLiteral("outputDirectory")).toString();
    result.outputName = settings.value(QStringLiteral("outputName")).toString();
    
    // Geometry and stereo settings
    result.stereo = settings.value(QStringLiteral("stereo")).toBool();
    result.upsideDown = settings.value(QStringLiteral("upsideDown")).toBool();
    result.warping = settings.value(QStringLiteral("warping")).toBool();
    result.blendMask = settings.value(QStringLiteral("blendMask")).toBool();
    result.startIndex = settings.value(QStringLiteral("startIndex")).toInt();
    result.stopIndex = settings.value(QStringLiteral("stopIndex")).toInt();
    result.steps = settings.value(QStringLiteral("steps")).toInt(1);
    result.outputCount = settings.value(QStringLiteral("outputCount")).toInt(1);
    
    // Output names and enabled flags (restore from JSON arrays)
    const QJsonArray outputNamesArr = settings.value(QStringLiteral("outputNames")).toArray();
    for (const QJsonValue &nameVal : outputNamesArr) {
        result.outputNames.append(nameVal.toString());
    }
    const QJsonArray outputEnabledArr = settings.value(QStringLiteral("outputEnabled")).toArray();
    for (const QJsonValue &enabledVal : outputEnabledArr) {
        result.outputEnabled.append(enabledVal.toBool(false));
    }
    
    // Codec and encoding settings
    result.codec = settings.value(QStringLiteral("codec")).toString();
    result.preset = settings.value(QStringLiteral("preset")).toString();
    result.softwarePreset = settings.value(QStringLiteral("softwarePreset")).toString(result.softwarePreset);
    result.nvencPreset = settings.value(QStringLiteral("nvencPreset")).toString(result.nvencPreset);
    result.libxTune = settings.value(QStringLiteral("libxTune")).toString(result.libxTune);
    result.nvencTune = settings.value(QStringLiteral("nvencTune")).toString(result.nvencTune);
    result.nvencHardwareFrames = settings.value(QStringLiteral("nvencHardwareFrames")).toBool();
    result.encodingBitDepth = settings.value(QStringLiteral("encodingBitDepth")).toInt(result.encodingBitDepth);
    result.pixrate = settings.value(QStringLiteral("pixrate")).toInt(result.pixrate);
    result.constantQuality = settings.value(QStringLiteral("constantQuality")).toInt(result.constantQuality);
    result.crf = settings.value(QStringLiteral("crf")).toInt(result.crf);
    result.cq = settings.value(QStringLiteral("cq")).toInt(result.cq);
    result.qscale = settings.value(QStringLiteral("qscale")).toInt(result.qscale);
    
    // Frame rate settings
    result.frameRateNum = settings.value(QStringLiteral("frameRateNum")).toInt(1);
    result.frameRateDen = settings.value(QStringLiteral("frameRateDen")).toInt(30);
    result.inputFrameRateNum = settings.value(QStringLiteral("inputFrameRateNum")).toInt(30);
    result.inputFrameRateDen = settings.value(QStringLiteral("inputFrameRateDen")).toInt(1);
    
    // I-frame control settings
    result.useOnlyIframes = settings.value(QStringLiteral("useOnlyIframes")).toBool();
    result.gopSizeSeconds = settings.value(QStringLiteral("gopSizeSeconds")).toDouble(result.gopSizeSeconds);
    
    // Container preferences
    result.preferNtscOutputFrameRates = settings.value(QStringLiteral("preferNtscOutputFrameRates")).toBool();
    result.preferMatroska = settings.value(QStringLiteral("preferMatroska")).toBool();
    
    // Mapping and surface settings
    result.mappingMode = settings.value(QStringLiteral("mappingMode")).toString(result.mappingMode);
    result.surfaceRadius = settings.value(QStringLiteral("surfaceRadius")).toDouble(740.0);
    result.surfaceFov = settings.value(QStringLiteral("surfaceFov")).toDouble(165.0);
    
    // Layer geometry settings
    result.layerStereoMode = settings.value(QStringLiteral("layerStereoMode")).toString(result.layerStereoMode);
    result.layerAlpha = settings.value(QStringLiteral("layerAlpha")).toInt(result.layerAlpha);
    result.layerRoiEnabled = settings.value(QStringLiteral("layerRoiEnabled")).toBool();
    result.layerRoiX = settings.value(QStringLiteral("layerRoiX")).toDouble(result.layerRoiX);
    result.layerRoiY = settings.value(QStringLiteral("layerRoiY")).toDouble(result.layerRoiY);
    result.layerRoiWidth = settings.value(QStringLiteral("layerRoiWidth")).toDouble(result.layerRoiWidth);
    result.layerRoiHeight = settings.value(QStringLiteral("layerRoiHeight")).toDouble(result.layerRoiHeight);
    result.layerPitch = settings.value(QStringLiteral("layerPitch")).toDouble(result.layerPitch);
    result.layerYaw = settings.value(QStringLiteral("layerYaw")).toDouble(result.layerYaw);
    result.layerRoll = settings.value(QStringLiteral("layerRoll")).toDouble(result.layerRoll);
    
    // Plane geometry settings
    result.planeAzimuth = settings.value(QStringLiteral("planeAzimuth")).toDouble(result.planeAzimuth);
    result.planeElevation = settings.value(QStringLiteral("planeElevation")).toDouble(result.planeElevation);
    result.planeRoll = settings.value(QStringLiteral("planeRoll")).toDouble(result.planeRoll);
    result.planeDistance = settings.value(QStringLiteral("planeDistance")).toDouble(result.planeDistance);
    result.planeHorizontal = settings.value(QStringLiteral("planeHorizontal")).toDouble(result.planeHorizontal);
    result.planeVertical = settings.value(QStringLiteral("planeVertical")).toDouble(result.planeVertical);
    result.planeWidth = settings.value(QStringLiteral("planeWidth")).toDouble(result.planeWidth);
    result.planeHeight = settings.value(QStringLiteral("planeHeight")).toDouble(result.planeHeight);
    result.planeAspectRatio = settings.value(QStringLiteral("planeAspectRatio")).toInt(result.planeAspectRatio);
    
    // Performance settings
    result.maxEncoderThreads = settings.value(QStringLiteral("maxEncoderThreads")).toInt(result.maxEncoderThreads);
    result.imageBufferingThreadCount = settings.value(QStringLiteral("imageBufferingThreadCount")).toInt(result.imageBufferingThreadCount);
    result.captureGpuSlots = settings.value(QStringLiteral("captureGpuSlots")).toInt(result.captureGpuSlots);
    result.imageSizeWarningPercent = settings.value(QStringLiteral("imageSizeWarningPercent")).toInt(result.imageSizeWarningPercent);
    
    // Processing options
    result.runWithoutEncoding = settings.value(QStringLiteral("runWithoutEncoding")).toBool();
    result.runWithoutReadback = settings.value(QStringLiteral("runWithoutReadback")).toBool();
    result.outputContainerSuffix = settings.value(QStringLiteral("outputContainerSuffix")).toString();
    result.imageErrorBehavior = settings.value(QStringLiteral("imageErrorBehavior")).toString(result.imageErrorBehavior);
    result.videoDecodingMode = settings.value(QStringLiteral("videoDecodingMode")).toString(result.videoDecodingMode);
    
    // Parameter file
    result.parameterFile = settings.value(QStringLiteral("parameterFile")).toString();
    
    if (result.jobName.isEmpty()) result.jobName = result.outputName.isEmpty() ? result.jobId : result.outputName;
    *info = result; return true;
}

bool SliceController::saveInternalQueue(const QString &filePath)
{
    QString error; if (!canPersistInternalQueue(&error)) { qCWarning(cjobClient) << error; return false; }
    QJsonArray jobs; for (const JobInfo &job : m_queue) jobs.append(jobInfoToJson(job));
    QSaveFile file(filePath); if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("c-slice-job-queue")}, {QStringLiteral("version"), 1}, {QStringLiteral("jobs"), jobs}}).toJson());
    return file.commit();
}

bool SliceController::loadInternalQueue(const QString &filePath)
{
    QString error; if (!canPersistInternalQueue(&error)) { qCWarning(cjobClient) << error; return false; }
    QFile file(filePath); if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("type")).toString() != QLatin1String("c-slice-job-queue") || root.value(QStringLiteral("version")).toInt() != 1) return false;
    QVector<JobInfo> jobs; QMap<QString, JobInfo> stored;
    for (const QJsonValue &value : root.value(QStringLiteral("jobs")).toArray()) { JobInfo job; if (!jobInfoFromJson(value.toObject(), &job, &error) || stored.contains(job.jobId)) return false; jobs.append(job); stored.insert(job.jobId, job); }
    m_queue = jobs; m_storedJobs = stored; m_runningJobIndex = -1; Q_EMIT runningJobIndexChanged(); Q_EMIT internalQueueChanged(); return true;
}

void SliceController::setInternalQueueRunning(bool running)
{
    if (isEditingQueuedJob()) {
        qCWarning(cjobClient) << "Cannot change queue run state while editing a queued job";
        return;
    }

    if (m_internalQueueRunning == running) {
        return;
    }
    m_internalQueueRunning = running;
    Q_EMIT internalQueueRunningChanged();

    // Auto-start the first job when enabling the queue (only for internal queue, not C-Job)
    if (running && !m_cjobEnabled && !m_queue.isEmpty()) {
        qCInfo(cjobClient) << "Internal queue enabled - starting first job";
        QMetaObject::invokeMethod(this, [this]() {
            launchNextFromQueue();
        }, Qt::QueuedConnection);
    }
}

void SliceController::queueJob()
{
    if (isEditingQueuedJob()) {
        qCWarning(cjobClient) << "Cannot queue a job while editing a queued job";
        return;
    }

    // Generate a unique random job ID (UUID-style) for generic identification
    QString jobId = QStringLiteral("job-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')));

    // Generate descriptive name from current settings
    QString jobName = generateJobName();

    // Create full JobInfo with all parameters and store locally keyed by job ID
    JobInfo info = captureCurrentJobSettings();
    info.jobId = jobId;
    info.jobName = jobName;
    info.instanceId = QCoreApplication::applicationName() + QLatin1Char('-') + QString::number(QRandomGenerator::global()->generate());
    info.status = QStringLiteral("Pending");
    info.progressCompleted = 0;
    info.progressTotal = expectedSliceFrameCount();

    // Store full job parameters locally keyed by unique job ID
    // This allows retrieval when C-Job sends a launch command with just the job ID
    m_storedJobs[jobId] = info;

    // Add to internal queue (display only, not used for parameter storage)
    m_queue.append(info);

    qCInfo(cjobClient) << "Job queued:" << jobName << "(ID:" << jobId << ", total frames:" << info.progressTotal << ")";

    Q_EMIT internalQueueChanged();
    Q_EMIT runningJobIndexChanged();

    // If C-Job is enabled, submit to server (only job ID + name)
    if (m_cjobEnabled && m_cjobSocket && m_cjobSocket->isOpen()) {
        submitToCJob(jobId);
    }
}

void SliceController::reorderInternalQueue(int start, int destination)
{
    if (isEditingQueuedJob()) {
        qCWarning(cjobClient) << "Cannot reorder the queue while editing a queued job";
        return;
    }

    if (start < 0 || start >= m_queue.size() || destination < 0 || destination >= m_queue.size() || start == destination) {
        return;
    }

    // Move the job from 'start' position to 'destination' position in internal queue
    const JobInfo job = m_queue.takeAt(start);
    const int insertPos = qMin(destination, m_queue.size());
    m_queue.insert(insertPos, job);

    qCInfo(cjobClient) << "Reordered queue: moved job from index" << start << "to" << insertPos;

    Q_EMIT internalQueueChanged();
}

void SliceController::removeJobFromInternalQueue(int index)
{
    if (isEditingQueuedJob()) {
        qCWarning(cjobClient) << "Cannot remove a job while editing a queued job";
        return;
    }

    if (index < 0 || index >= m_queue.size()) {
        return;
    }

    // Only allow removing from queue if not currently running
    if (running()) {
        qCWarning(cjobClient) << "Cannot remove job while a slice is running";
        return;
    }

    const JobInfo removedJob = m_queue.takeAt(index);

    // Also remove stored parameters for this job
    m_storedJobs.remove(removedJob.jobId);

    qCInfo(cjobClient) << "Removed job from internal queue at index" << index << "(ID:" << removedJob.jobId << ")";

    Q_EMIT internalQueueChanged();
}

bool SliceController::beginQueuedJobEdit(int index)
{
    if (isEditingQueuedJob() || m_cjobEnabled || running() || m_internalQueueRunning
        || index < 0 || index >= m_queue.size() || index == m_runningJobIndex) {
        qCWarning(cjobClient) << "Cannot edit queued job at index" << index;
        return false;
    }

    const JobInfo &job = m_queue.at(index);
    if (job.status != QLatin1String("Pending")) {
        qCWarning(cjobClient) << "Cannot edit non-pending queued job:" << job.jobId;
        return false;
    }

    m_preEditJobSettings = captureCurrentJobSettings();
    m_editedQueuedJobId = job.jobId;
    m_editedQueuedJobName = job.jobName;
    applyJobSettings(job);
    Q_EMIT editingQueuedJobChanged();
    return true;
}

bool SliceController::saveQueuedJobEdit()
{
    if (!isEditingQueuedJob() || m_cjobEnabled || running() || m_internalQueueRunning) {
        qCWarning(cjobClient) << "Cannot save queued job edit";
        return false;
    }

    for (JobInfo &job : m_queue) {
        if (job.jobId != m_editedQueuedJobId) {
            continue;
        }

        if (job.status != QLatin1String("Pending")) {
            qCWarning(cjobClient) << "Cannot save a non-pending queued job:" << job.jobId;
            return false;
        }

        JobInfo updated = captureCurrentJobSettings();
        updated.jobId = job.jobId;
        updated.instanceId = job.instanceId;
        updated.status = job.status;
        updated.progressCompleted = job.progressCompleted;
        updated.progressTotal = expectedSliceFrameCount();
        updated.jobName = generateJobName();
        job = updated;
        m_storedJobs[updated.jobId] = updated;
        m_editedQueuedJobId.clear();
        m_editedQueuedJobName.clear();
        m_preEditJobSettings = JobInfo{};
        Q_EMIT internalQueueChanged();
        Q_EMIT editingQueuedJobChanged();
        return true;
    }

    qCWarning(cjobClient) << "Queued job to save was not found:" << m_editedQueuedJobId;
    return false;
}

bool SliceController::cancelQueuedJobEdit()
{
    if (!isEditingQueuedJob()) {
        return false;
    }

    applyJobSettings(m_preEditJobSettings);
    m_preEditJobSettings = JobInfo{};
    m_editedQueuedJobId.clear();
    m_editedQueuedJobName.clear();
    Q_EMIT editingQueuedJobChanged();
    return true;
}

bool SliceController::submitToCJob(const QString &jobId)
{
    if (!m_cjobSocket || !m_cjobSocket->isOpen()) {
        qCWarning(cjobClient) << "Cannot submit to C-Job: not connected";
        return false;
    }

    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("submit_job");
    json[QStringLiteral("instance_id")] = QString(QCoreApplication::applicationName() + QLatin1Char('-') + QString::number(QRandomGenerator::global()->generate()));
    json[QStringLiteral("job_id")] = jobId;
    const auto job = std::find_if(m_queue.cbegin(), m_queue.cend(), [&jobId](const JobInfo &info) {
        return info.jobId == jobId;
    });
    json[QStringLiteral("job_name")] = job != m_queue.cend() ? job->jobName : generateJobName();

    sendJsonToCJob(json);
    qCInfo(cjobClient) << "Submitted job to C-Job:" << jobId << "(" << json.value(QStringLiteral("job_name")).toString() << ")";
    return true;
}

void SliceController::launchNextFromQueue()
{
    if (isEditingQueuedJob()) {
        qCWarning(cjobClient) << "Cannot launch a queued job while editing a queued job";
        return;
    }

    if (m_queue.isEmpty()) {
        qCWarning(cjobClient) << "No jobs in queue";
        return;
    }

    // Get the first job from queue but keep it in the queue for status tracking
    JobInfo info = m_queue.first();
    const JobInfo originalJobSettings = captureCurrentJobSettings();

    // Temporarily store current settings
    QString origInputType = m_inputType;
    QString origLeftInput = m_leftInput;
    QString origRightInput = m_rightInput;
    QString origOutputDirectory = m_outputDirectory;
    QString origOutputName = m_outputName;
    bool origStereo = m_stereo;
    bool origUpsideDown = m_upsideDown;
    bool origWarping = m_warping;
    bool origBlendMask = m_blendMask;
    int origStartIndex = m_startIndex;
    int origStopIndex = m_stopIndex;
    int origSteps = m_steps;
    QString origMappingMode = m_mappingMode;
    double origSurfaceRadius = m_surfaceRadius;
    double origSurfaceFov = m_surfaceFov;
    QString origLayerStereoMode = m_layerStereoMode;
    int origLayerAlpha = m_layerAlpha;
    bool origLayerRoiEnabled = m_layerRoiEnabled;
    double origLayerRoiX = m_layerRoiX;
    double origLayerRoiY = m_layerRoiY;
    double origLayerRoiWidth = m_layerRoiWidth;
    double origLayerRoiHeight = m_layerRoiHeight;
    double origLayerPitch = m_layerPitch;
    double origLayerYaw = m_layerYaw;
    double origLayerRoll = m_layerRoll;
    double origPlaneAzimuth = m_planeAzimuth;
    double origPlaneElevation = m_planeElevation;
    double origPlaneRoll = m_planeRoll;
    double origPlaneDistance = m_planeDistance;
    double origPlaneHorizontal = m_planeHorizontal;
    double origPlaneVertical = m_planeVertical;
    double origPlaneWidth = m_planeWidth;
    double origPlaneHeight = m_planeHeight;
    int origPlaneAspectRatio = m_planeAspectRatio;
    QString origCodec = m_codec;
    QString origPreset = preset();
    QString origSoftwarePreset = m_softwarePreset;
    QString origNvencPreset = m_nvencPreset;
    QString origLibxTune = m_libxTune;
    QString origNvencTune = m_nvencTune;
    bool origNvencHardwareFrames = m_nvencHardwareFrames;
    int origEncodingBitDepth = m_encodingBitDepth;
    int origPixrate = m_pixrate;
    int origConstantQuality = m_constantQuality;
    int origCrf = m_crf;
    int origCq = m_cq;
    int origQscale = m_qscale;
    int origFrameRateNum = m_frameRateNum;
    int origFrameRateDen = m_frameRateDen;
    QString origParameterFile = m_parameterFile;
    bool origUseOnlyIframes = m_useOnlyIframes;
    double origGopSizeSeconds = m_gopSizeSeconds;
    bool origPreferMatroska = m_preferMatroska;
    QString origOutputContainerSuffix = outputContainerSuffix();
    int origMaxEncoderThreads = m_maxEncoderThreads;
    int origImageBufferingThreadCount = m_imageBufferingThreadCount;
    int origCaptureGpuSlots = m_captureGpuSlots;

    // Apply job parameters
    setInputType(info.inputType);
    setLeftInput(info.leftInput);
    setRightInput(info.rightInput);
    setOutputDirectory(info.outputDirectory);
    setOutputName(info.outputName);
    setStereo(info.stereo);
    setUpsideDown(info.upsideDown);
    setWarping(info.warping);
    setBlendMask(info.blendMask);
    setStartIndex(info.startIndex);
    setStopIndex(info.stopIndex);
    setSteps(info.steps);
    setMappingMode(info.mappingMode);
    setSurfaceRadius(info.surfaceRadius);
    setSurfaceFov(info.surfaceFov);
    setLayerStereoMode(info.layerStereoMode);
    setLayerAlpha(info.layerAlpha);
    setLayerRoiEnabled(info.layerRoiEnabled);
    setLayerRoiX(info.layerRoiX);
    setLayerRoiY(info.layerRoiY);
    setLayerRoiWidth(info.layerRoiWidth);
    setLayerRoiHeight(info.layerRoiHeight);
    setLayerPitch(info.layerPitch);
    setLayerYaw(info.layerYaw);
    setLayerRoll(info.layerRoll);
    setPlaneAzimuth(info.planeAzimuth);
    setPlaneElevation(info.planeElevation);
    setPlaneRoll(info.planeRoll);
    setPlaneDistance(info.planeDistance);
    setPlaneHorizontal(info.planeHorizontal);
    setPlaneVertical(info.planeVertical);
    setPlaneWidth(info.planeWidth);
    setPlaneHeight(info.planeHeight);
    setPlaneAspectRatio(info.planeAspectRatio);
    setCodec(info.codec);
    setPreset(info.preset);
    setSoftwarePreset(info.softwarePreset);
    setNvencPreset(info.nvencPreset);
    setLibxTune(info.libxTune);
    setNvencTune(info.nvencTune);
    setNvencHardwareFrames(info.nvencHardwareFrames);
    setEncodingBitDepth(info.encodingBitDepth);
    setPixrate(info.pixrate);
    setConstantQuality(info.constantQuality);
    setCrf(info.crf);
    setCq(info.cq);
    setQscale(info.qscale);
    setFrameRateNum(info.frameRateNum);
    setFrameRateDen(info.frameRateDen);
    setParameterFile(info.parameterFile);
    setUseOnlyIframes(info.useOnlyIframes);
    setGopSizeSeconds(info.gopSizeSeconds);
    setPreferMatroska(info.preferMatroska);
    setOutputContainerSuffix(info.outputContainerSuffix);
    setMaxEncoderThreads(info.maxEncoderThreads);
    setImageBufferingThreadCount(info.imageBufferingThreadCount);
    setCaptureGpuSlots(info.captureGpuSlots);

    qCInfo(cjobClient) << "Launching queued job:" << info.jobName;

    // Track which index is running (always 0 for next-from-queue)
    m_runningJobIndex = 0;
    Q_EMIT runningJobIndexChanged();

    // Apply settings omitted by the legacy per-property block before launch.
    applyJobSettings(info);

    // Launch the slice with job parameters
    launchSlice();

    // Restore original settings after launch
    QMetaObject::invokeMethod(this, [this, origLeftInput, origRightInput, origOutputDirectory, origOutputName,
        origInputType, origStereo, origUpsideDown, origWarping, origBlendMask,
        origStartIndex, origStopIndex, origSteps, origMappingMode,
        origSurfaceRadius, origSurfaceFov, origLayerStereoMode, origLayerAlpha,
        origLayerRoiEnabled, origLayerRoiX, origLayerRoiY, origLayerRoiWidth, origLayerRoiHeight,
        origLayerPitch, origLayerYaw, origLayerRoll,
        origPlaneAzimuth, origPlaneElevation, origPlaneRoll,
        origPlaneDistance, origPlaneHorizontal, origPlaneVertical,
        origPlaneWidth, origPlaneHeight, origPlaneAspectRatio,
        origCodec, origPreset, origSoftwarePreset, origNvencPreset,
        origLibxTune, origNvencTune, origNvencHardwareFrames,
        origEncodingBitDepth, origPixrate, origConstantQuality,
        origCrf, origCq, origQscale,
        origFrameRateNum, origFrameRateDen, origParameterFile,
        origUseOnlyIframes, origGopSizeSeconds, origPreferMatroska,
        origOutputContainerSuffix, origMaxEncoderThreads,
        origImageBufferingThreadCount, origCaptureGpuSlots, originalJobSettings]() {
        setInputType(origInputType);
        setLeftInput(origLeftInput);
        setRightInput(origRightInput);
        setOutputDirectory(origOutputDirectory);
        setOutputName(origOutputName);
        setStereo(origStereo);
        setUpsideDown(origUpsideDown);
        setWarping(origWarping);
        setBlendMask(origBlendMask);
        setStartIndex(origStartIndex);
        setStopIndex(origStopIndex);
        setSteps(origSteps);
        setMappingMode(origMappingMode);
        setSurfaceRadius(origSurfaceRadius);
        setSurfaceFov(origSurfaceFov);
        setLayerStereoMode(origLayerStereoMode);
        setLayerAlpha(origLayerAlpha);
        setLayerRoiEnabled(origLayerRoiEnabled);
        setLayerRoiX(origLayerRoiX);
        setLayerRoiY(origLayerRoiY);
        setLayerRoiWidth(origLayerRoiWidth);
        setLayerRoiHeight(origLayerRoiHeight);
        setLayerPitch(origLayerPitch);
        setLayerYaw(origLayerYaw);
        setLayerRoll(origLayerRoll);
        setPlaneAzimuth(origPlaneAzimuth);
        setPlaneElevation(origPlaneElevation);
        setPlaneRoll(origPlaneRoll);
        setPlaneDistance(origPlaneDistance);
        setPlaneHorizontal(origPlaneHorizontal);
        setPlaneVertical(origPlaneVertical);
        setPlaneWidth(origPlaneWidth);
        setPlaneHeight(origPlaneHeight);
        setPlaneAspectRatio(origPlaneAspectRatio);
        setCodec(origCodec);
        setPreset(origPreset);
        setSoftwarePreset(origSoftwarePreset);
        setNvencPreset(origNvencPreset);
        setLibxTune(origLibxTune);
        setNvencTune(origNvencTune);
        setNvencHardwareFrames(origNvencHardwareFrames);
        setEncodingBitDepth(origEncodingBitDepth);
        setPixrate(origPixrate);
        setConstantQuality(origConstantQuality);
        setCrf(origCrf);
        setCq(origCq);
        setQscale(origQscale);
        setFrameRateNum(origFrameRateNum);
        setFrameRateDen(origFrameRateDen);
        setParameterFile(origParameterFile);
        setUseOnlyIframes(origUseOnlyIframes);
        setGopSizeSeconds(origGopSizeSeconds);
        setPreferMatroska(origPreferMatroska);
        setOutputContainerSuffix(origOutputContainerSuffix);
        setMaxEncoderThreads(origMaxEncoderThreads);
        setImageBufferingThreadCount(origImageBufferingThreadCount);
        setCaptureGpuSlots(origCaptureGpuSlots);
        applyJobSettings(originalJobSettings);
    }, Qt::QueuedConnection);
}

void SliceController::launchJobAtQueueIndex(int index)
{
    if (isEditingQueuedJob()) {
        qCWarning(cjobClient) << "Cannot launch a queued job while editing a queued job";
        return;
    }

    if (index < 0 || index >= m_queue.size()) {
        qCWarning(cjobClient) << "Invalid queue index:" << index;
        return;
    }

    JobInfo info = m_queue[index];
    const JobInfo originalJobSettings = captureCurrentJobSettings();

    // Temporarily store current settings
    QString origInputType = m_inputType;
    QString origLeftInput = m_leftInput;
    QString origRightInput = m_rightInput;
    QString origOutputDirectory = m_outputDirectory;
    QString origOutputName = m_outputName;
    bool origStereo = m_stereo;
    bool origUpsideDown = m_upsideDown;
    bool origWarping = m_warping;
    bool origBlendMask = m_blendMask;
    int origStartIndex = m_startIndex;
    int origStopIndex = m_stopIndex;
    int origSteps = m_steps;
    QString origMappingMode = m_mappingMode;
    double origSurfaceRadius = m_surfaceRadius;
    double origSurfaceFov = m_surfaceFov;
    QString origLayerStereoMode = m_layerStereoMode;
    int origLayerAlpha = m_layerAlpha;
    bool origLayerRoiEnabled = m_layerRoiEnabled;
    double origLayerRoiX = m_layerRoiX;
    double origLayerRoiY = m_layerRoiY;
    double origLayerRoiWidth = m_layerRoiWidth;
    double origLayerRoiHeight = m_layerRoiHeight;
    double origLayerPitch = m_layerPitch;
    double origLayerYaw = m_layerYaw;
    double origLayerRoll = m_layerRoll;
    double origPlaneAzimuth = m_planeAzimuth;
    double origPlaneElevation = m_planeElevation;
    double origPlaneRoll = m_planeRoll;
    double origPlaneDistance = m_planeDistance;
    double origPlaneHorizontal = m_planeHorizontal;
    double origPlaneVertical = m_planeVertical;
    double origPlaneWidth = m_planeWidth;
    double origPlaneHeight = m_planeHeight;
    int origPlaneAspectRatio = m_planeAspectRatio;
    QString origCodec = m_codec;
    QString origPreset = preset();
    QString origSoftwarePreset = m_softwarePreset;
    QString origNvencPreset = m_nvencPreset;
    QString origLibxTune = m_libxTune;
    QString origNvencTune = m_nvencTune;
    bool origNvencHardwareFrames = m_nvencHardwareFrames;
    int origEncodingBitDepth = m_encodingBitDepth;
    int origPixrate = m_pixrate;
    int origConstantQuality = m_constantQuality;
    int origCrf = m_crf;
    int origCq = m_cq;
    int origQscale = m_qscale;
    int origFrameRateNum = m_frameRateNum;
    int origFrameRateDen = m_frameRateDen;
    QString origParameterFile = m_parameterFile;
    bool origUseOnlyIframes = m_useOnlyIframes;
    double origGopSizeSeconds = m_gopSizeSeconds;
    bool origPreferMatroska = m_preferMatroska;
    QString origOutputContainerSuffix = outputContainerSuffix();
    int origMaxEncoderThreads = m_maxEncoderThreads;
    int origImageBufferingThreadCount = m_imageBufferingThreadCount;
    int origCaptureGpuSlots = m_captureGpuSlots;

    // Apply job parameters
    setInputType(info.inputType);
    setLeftInput(info.leftInput);
    setRightInput(info.rightInput);
    setOutputDirectory(info.outputDirectory);
    setOutputName(info.outputName);
    setStereo(info.stereo);
    setUpsideDown(info.upsideDown);
    setWarping(info.warping);
    setBlendMask(info.blendMask);
    setStartIndex(info.startIndex);
    setStopIndex(info.stopIndex);
    setSteps(info.steps);
    setMappingMode(info.mappingMode);
    setSurfaceRadius(info.surfaceRadius);
    setSurfaceFov(info.surfaceFov);
    setLayerStereoMode(info.layerStereoMode);
    setLayerAlpha(info.layerAlpha);
    setLayerRoiEnabled(info.layerRoiEnabled);
    setLayerRoiX(info.layerRoiX);
    setLayerRoiY(info.layerRoiY);
    setLayerRoiWidth(info.layerRoiWidth);
    setLayerRoiHeight(info.layerRoiHeight);
    setLayerPitch(info.layerPitch);
    setLayerYaw(info.layerYaw);
    setLayerRoll(info.layerRoll);
    setPlaneAzimuth(info.planeAzimuth);
    setPlaneElevation(info.planeElevation);
    setPlaneRoll(info.planeRoll);
    setPlaneDistance(info.planeDistance);
    setPlaneHorizontal(info.planeHorizontal);
    setPlaneVertical(info.planeVertical);
    setPlaneWidth(info.planeWidth);
    setPlaneHeight(info.planeHeight);
    setPlaneAspectRatio(info.planeAspectRatio);
    setCodec(info.codec);
    setPreset(info.preset);
    setSoftwarePreset(info.softwarePreset);
    setNvencPreset(info.nvencPreset);
    setLibxTune(info.libxTune);
    setNvencTune(info.nvencTune);
    setNvencHardwareFrames(info.nvencHardwareFrames);
    setEncodingBitDepth(info.encodingBitDepth);
    setPixrate(info.pixrate);
    setConstantQuality(info.constantQuality);
    setCrf(info.crf);
    setCq(info.cq);
    setQscale(info.qscale);
    setFrameRateNum(info.frameRateNum);
    setFrameRateDen(info.frameRateDen);
    setParameterFile(info.parameterFile);
    setUseOnlyIframes(info.useOnlyIframes);
    setGopSizeSeconds(info.gopSizeSeconds);
    setPreferMatroska(info.preferMatroska);
    setOutputContainerSuffix(info.outputContainerSuffix);
    setMaxEncoderThreads(info.maxEncoderThreads);
    setImageBufferingThreadCount(info.imageBufferingThreadCount);
    setCaptureGpuSlots(info.captureGpuSlots);

    qCInfo(cjobClient) << "Launching queued job at index" << index << ":" << info.jobName;

    // Track which index is running
    m_runningJobIndex = index;
    Q_EMIT runningJobIndexChanged();

    // Apply settings omitted by the legacy per-property block before launch.
    applyJobSettings(info);

    // Launch the slice with job parameters
    launchSlice();

    // Restore original settings after launch
    QMetaObject::invokeMethod(this, [this, origLeftInput, origRightInput, origOutputDirectory, origOutputName,
        origInputType, origStereo, origUpsideDown, origWarping, origBlendMask,
        origStartIndex, origStopIndex, origSteps, origMappingMode,
        origSurfaceRadius, origSurfaceFov, origLayerStereoMode, origLayerAlpha,
        origLayerRoiEnabled, origLayerRoiX, origLayerRoiY, origLayerRoiWidth, origLayerRoiHeight,
        origLayerPitch, origLayerYaw, origLayerRoll,
        origPlaneAzimuth, origPlaneElevation, origPlaneRoll,
        origPlaneDistance, origPlaneHorizontal, origPlaneVertical,
        origPlaneWidth, origPlaneHeight, origPlaneAspectRatio,
        origCodec, origPreset, origSoftwarePreset, origNvencPreset,
        origLibxTune, origNvencTune, origNvencHardwareFrames,
        origEncodingBitDepth, origPixrate, origConstantQuality,
        origCrf, origCq, origQscale,
        origFrameRateNum, origFrameRateDen, origParameterFile,
        origUseOnlyIframes, origGopSizeSeconds, origPreferMatroska,
        origOutputContainerSuffix, origMaxEncoderThreads,
        origImageBufferingThreadCount, origCaptureGpuSlots, originalJobSettings]() {
        setInputType(origInputType);
        setLeftInput(origLeftInput);
        setRightInput(origRightInput);
        setOutputDirectory(origOutputDirectory);
        setOutputName(origOutputName);
        setStereo(origStereo);
        setUpsideDown(origUpsideDown);
        setWarping(origWarping);
        setBlendMask(origBlendMask);
        setStartIndex(origStartIndex);
        setStopIndex(origStopIndex);
        setSteps(origSteps);
        setMappingMode(origMappingMode);
        setSurfaceRadius(origSurfaceRadius);
        setSurfaceFov(origSurfaceFov);
        setLayerStereoMode(origLayerStereoMode);
        setLayerAlpha(origLayerAlpha);
        setLayerRoiEnabled(origLayerRoiEnabled);
        setLayerRoiX(origLayerRoiX);
        setLayerRoiY(origLayerRoiY);
        setLayerRoiWidth(origLayerRoiWidth);
        setLayerRoiHeight(origLayerRoiHeight);
        setLayerPitch(origLayerPitch);
        setLayerYaw(origLayerYaw);
        setLayerRoll(origLayerRoll);
        setPlaneAzimuth(origPlaneAzimuth);
        setPlaneElevation(origPlaneElevation);
        setPlaneRoll(origPlaneRoll);
        setPlaneDistance(origPlaneDistance);
        setPlaneHorizontal(origPlaneHorizontal);
        setPlaneVertical(origPlaneVertical);
        setPlaneWidth(origPlaneWidth);
        setPlaneHeight(origPlaneHeight);
        setPlaneAspectRatio(origPlaneAspectRatio);
        setCodec(origCodec);
        setPreset(origPreset);
        setSoftwarePreset(origSoftwarePreset);
        setNvencPreset(origNvencPreset);
        setLibxTune(origLibxTune);
        setNvencTune(origNvencTune);
        setNvencHardwareFrames(origNvencHardwareFrames);
        setEncodingBitDepth(origEncodingBitDepth);
        setPixrate(origPixrate);
        setConstantQuality(origConstantQuality);
        setCrf(origCrf);
        setCq(origCq);
        setQscale(origQscale);
        setFrameRateNum(origFrameRateNum);
        setFrameRateDen(origFrameRateDen);
        setParameterFile(origParameterFile);
        setUseOnlyIframes(origUseOnlyIframes);
        setGopSizeSeconds(origGopSizeSeconds);
        setPreferMatroska(origPreferMatroska);
        setOutputContainerSuffix(origOutputContainerSuffix);
        setMaxEncoderThreads(origMaxEncoderThreads);
        setImageBufferingThreadCount(origImageBufferingThreadCount);
        setCaptureGpuSlots(origCaptureGpuSlots);
        applyJobSettings(originalJobSettings);
    }, Qt::QueuedConnection);
}

// Called when a process finishes - handle queue auto-advance
void SliceController::handleProcessFinish()
{
    // If we were processing a job from C-Job, notify the server that it's complete
    if (!m_currentCJobJobId.isEmpty()) {
        qCInfo(cjobClient) << "Job complete, notifying C-Job server";

        QJsonObject json;
        json[QStringLiteral("type")] = QStringLiteral("complete_job");
        json[QStringLiteral("job_id")] = m_currentCJobJobId;

        sendJsonToCJob(json);
        m_currentCJobJobId.clear();
    }

    // Remove the completed job from the queue at the running job index
    if (!m_queue.isEmpty()) {
        const int removeIndex = (m_runningJobIndex >= 0 && m_runningJobIndex < m_queue.size()) ? m_runningJobIndex : 0;
        m_queue.remove(removeIndex);

        // Reset running job index after completion
        if (m_runningJobIndex >= 0) {
            m_runningJobIndex = -1;
            Q_EMIT runningJobIndexChanged();
        }

        Q_EMIT internalQueueChanged();

        // If internal queue is running and there are more jobs, start the next one
        if (m_internalQueueRunning && !m_queue.isEmpty() && !m_cjobEnabled) {
            qCInfo(cjobClient) << "Internal queue running - starting next job";
            QMetaObject::invokeMethod(this, [this]() {
                launchNextFromQueue();
            }, Qt::QueuedConnection);
        }
    } else {
        // Queue is empty after removal - reset running index
        if (m_runningJobIndex >= 0) {
            m_runningJobIndex = -1;
            Q_EMIT runningJobIndexChanged();
        }
    }
}
