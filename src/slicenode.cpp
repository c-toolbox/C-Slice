/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "slicenode.h"

#include "ffmpegprobe.h"
#include "slicecaptureoutput.h"
#include "sliceencoder.h"
#include "sliceimageloader.h"
#include "slicevideoloader.h"
#include "slicerenderer.h"
#include "utils/imagesequenceutils.h"

#include <sgct/opengl.h>
#include <sgct/sgct.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr int DefaultReadbackSlotCount = 4;

enum class MappingMode {
    Dome,
    SphereEqr,
    SphereEac,
    Plane
};

enum class ImageErrorBehavior {
    Abort,
    Pause,
    Continue
};

enum class VideoDecodingMode {
    Software,
    Hardware,
    Hybrid
};

enum class SliceInputType {
    Image,
    Video
};

struct SliceJob {
    SliceInputType inputType = SliceInputType::Image;
    std::optional<std::string> config;
    std::string left;
    std::string right;
    std::vector<std::pair<std::string, std::string>> outputs;
    int start = 0;
    int stop = 0;
    int steps = 1;
    int maxEncoderThreads = 16;
    int pixrate = 6;
    int constantQuality = 23;
    int frameRateNum = 1;
    int frameRateDen = 30;
    int inputFrameRateNum = 30;
    int inputFrameRateDen = 1;
    int imageDelayMs = 0;
    int imageBufferingThreadCount = 16;
    int captureGpuSlots = DefaultReadbackSlotCount;
    int imageSizeWarningPercent = 25;
    bool noEncode = false;
    bool noReadback = false;
    bool upsideDown = false;
    bool warping = false;
    bool blendMask = false;
    MappingMode mappingMode = MappingMode::Dome;
    double surfaceRadius = 740.0;
    double surfaceFov = 165.0;
    CSlice::StereoMode layerStereoMode = CSlice::StereoMode::None;
    int layerAlpha = 100;
    bool layerRoiEnabled = false;
    glm::vec4 layerRoi = glm::vec4(0.f, 0.f, 1.f, 1.f);
    glm::vec3 layerRotate = glm::vec3(0.f);
    double planeAzimuth = 0.0;
    double planeElevation = 0.0;
    double planeRoll = 0.0;
    double planeDistance = 740.0;
    double planeHorizontal = 0.0;
    double planeVertical = 0.0;
    double planeWidth = 0.0;
    double planeHeight = 0.0;
    std::uint8_t planeAspectRatio = 1;
    std::string codec = "H264";
    std::string preset = "ultrafast";
    std::string libxTune = "fastdecode";
    std::string nvencTune = "High quality";
    bool nvencHardwareFrames = false;
    int encodingBitDepth = 8;
    std::string parameterFile;
    ImageErrorBehavior imageErrorBehavior = ImageErrorBehavior::Abort;
    VideoDecodingMode videoDecodingMode = VideoDecodingMode::Software;
    bool verifyOnly = false;
    bool useOnlyIframes = false;
    int gopSize = 1;
};

SliceJob job;
std::unique_ptr<CSlice::SliceMediaLoader> sliceLoader;
std::unique_ptr<CSlice::SliceRenderer> SliceRenderer;
std::vector<CSlice::SliceCaptureOutput> windowOutputs;
int currentFrameIndex = 0;
int totalFrames = 1;
int capturedFrames = 0;
int renderedFrames = 0;
int lastReportedLoadedFrames = -1;
int lastReportedRenderedFrames = -1;
double lastProgressReportTime = -1.0;
bool shouldCapture = false;
bool receivedTerminate = false;
bool externalConnected = false;
std::atomic_bool pauseRequested = false;
bool loaderPauseApplied = false;
double startTime = 0.0;
std::chrono::steady_clock::time_point verificationStartTime;
AVCodecID desiredCodec = AV_CODEC_ID_H264;
bool movieOutput = true;
std::vector<CSlice::SliceRenderer::WarpWindowConfig> warpWindowConfigs;

bool movieReadbackQueueFull();
CSlice::SliceSourceOptions sourceOptions(const std::string &path,
                                                 std::string identifier,
                                                 bool validateSequenceFrames = true);

ImageErrorBehavior imageErrorBehaviorFromString(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (value == "pause") {
        return ImageErrorBehavior::Pause;
    }
    if (value == "continue") {
        return ImageErrorBehavior::Continue;
    }
    return ImageErrorBehavior::Abort;
}

VideoDecodingMode videoDecodingModeFromString(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (value == "hardware") {
        return VideoDecodingMode::Hardware;
    }
    if (value == "hybrid") {
        return VideoDecodingMode::Hybrid;
    }
    return VideoDecodingMode::Software;
}

std::string videoDecodingModeString(VideoDecodingMode mode)
{
    switch (mode) {
        case VideoDecodingMode::Hardware: return "Hardware";
        case VideoDecodingMode::Hybrid: return "Hybrid";
        case VideoDecodingMode::Software:
        default: return "Software";
    }
}

SliceInputType inputTypeFromString(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (value == "video") {
        return SliceInputType::Video;
    }
    return SliceInputType::Image;
}

std::string inputTypeString(SliceInputType inputType)
{
    return inputType == SliceInputType::Video ? "Video" : "Image sequence";
}

void startCommandInputThread()
{
    std::thread([]() {
        std::string command;
        while (std::getline(std::cin, command)) {
            std::ranges::transform(command, command.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (command == "pause") {
                pauseRequested = true;
                std::cout << "Paused\n" << std::flush;
            }
            else if (command == "resume") {
                pauseRequested = false;
                std::cout << "Resumed\n" << std::flush;
            }
            else if (command == "abort" || command == "terminate") {
                receivedTerminate = true;
            }
        }
    }).detach();
}

void reportCriticalError(const std::string &message, const std::filesystem::path &path = {})
{
    std::string fullMessage = message;
    if (!path.empty()) {
        fullMessage += std::format(" ({})", path.string());
    }

    std::cout << std::format("CriticalError {}", fullMessage);
    std::cout << "\n" << std::flush;
    std::cerr << fullMessage;
    std::cerr << "\n";
}

void reportVerificationProgress(int verifiedFrames, int totalFramesNum, int width, int height, void *userData)
{
    const std::string *identifier = static_cast<const std::string *>(userData);
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - verificationStartTime;
    std::cout << std::format("Verified {} {} {} {} {} {:.1f}s\n",
        identifier ? *identifier : std::string("input"),
        verifiedFrames,
        totalFramesNum,
        width,
        height,
        elapsed.count()) << std::flush;
}

std::string normalizedCodecName(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::ranges::replace(value, '_', ' ');
    std::ranges::replace(value, '-', ' ');
    return value;
}

std::string hardwareEncoderName(const std::string &codec)
{
    const std::string normalized = normalizedCodecName(codec);
    if (normalized == "h264 nvenc") {
        return "h264_nvenc";
    }
    if (normalized == "h265 nvenc" || normalized == "hevc nvenc") {
        return "hevc_nvenc";
    }
    return {};
}

bool isNvencCodec(const SliceJob &j)
{
    return !hardwareEncoderName(j.codec).empty();
}

bool hasValue(const std::vector<std::string> &arguments, size_t index)
{
    return index + 1 < arguments.size();
}

bool isUnsignedInteger(std::string_view value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

int toInt(const std::string &value, int fallback);

bool outputIdentifierMatchesWindow(std::string_view identifier,
                                   const sgct::config::Window &window,
                                   int originalIndex)
{
    if (identifier.empty()) {
        return false;
    }

    if (isUnsignedInteger(identifier)) {
        const int index = toInt(std::string(identifier), -1);
        return index == originalIndex || index == window.id;
    }

    return window.name && *window.name == identifier;
}

bool outputRequestedForWindow(const sgct::config::Window &window, int originalIndex)
{
    return std::any_of(job.outputs.begin(), job.outputs.end(), [&window, originalIndex](const auto &outputSpec) {
        return outputIdentifierMatchesWindow(outputSpec.first, window, originalIndex);
    });
}

int toInt(const std::string &value, int fallback)
{
    try {
        return std::stoi(value);
    }
    catch (...) {
        return fallback;
    }
}

double toDouble(const std::string &value, double fallback)
{
    try {
        return std::stod(value);
    }
    catch (...) {
        return fallback;
    }
}

MappingMode mappingModeFromString(std::string mode)
{
    std::ranges::transform(mode, mode.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::ranges::replace(mode, '_', ' ');
    std::ranges::replace(mode, '-', ' ');

    if (mode == "sphere eac" || mode == "eac") {
        return MappingMode::SphereEac;
    }
    if (mode == "sphere eqr" || mode == "sphere" || mode == "spherical" || mode == "eqr") {
        return MappingMode::SphereEqr;
    }
    if (mode == "plane") {
        return MappingMode::Plane;
    }
    return MappingMode::Dome;
}

CSlice::StereoMode stereoModeFromString(std::string mode)
{
    std::ranges::transform(mode, mode.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::ranges::replace(mode, '_', ' ');
    std::ranges::replace(mode, '-', ' ');

    if (mode == "3d (side by side)" || mode == "side by side" || mode == "sbs") {
        return CSlice::StereoMode::SideBySide;
    }
    if (mode == "3d (top bottom)" || mode == "top bottom" || mode == "tb") {
        return CSlice::StereoMode::TopBottom;
    }
    if (mode == "3d (top bottom+flip)" || mode == "top bottom+flip" || mode == "tb flip") {
        return CSlice::StereoMode::TopBottomFlip;
    }
    return CSlice::StereoMode::None;
}

CSlice::GridMode gridModeFromMappingMode(MappingMode mode)
{
    switch (mode) {
        case MappingMode::SphereEqr: return CSlice::GridMode::SphereEqr;
        case MappingMode::SphereEac: return CSlice::GridMode::SphereEac;
        case MappingMode::Plane: return CSlice::GridMode::Plane;
        case MappingMode::Dome:
        default: return CSlice::GridMode::Dome;
    }
}

int expectedFrameCount(const SliceJob &j)
{
    if (j.inputType == SliceInputType::Video) {
        const CSlice::SliceVideoLoader::Metadata metadata = CSlice::SliceVideoLoader::probeMetadata(j.left);
        if (metadata.ok) {
            const int stop = j.stop > 0 ? std::min(j.stop, metadata.frameCount - 1) : metadata.frameCount - 1;
            return std::max(1, ImageSequenceUtils::expectedFrameCount(j.start, stop, j.steps));
        }
    }
    return std::max(1, ImageSequenceUtils::expectedFrameCount(j.start, j.stop, j.steps));
}

void normalizeVideoFrameRange(SliceJob &j)
{
    if (j.inputType != SliceInputType::Video) {
        return;
    }
    const CSlice::SliceVideoLoader::Metadata metadata = CSlice::SliceVideoLoader::probeMetadata(j.left);
    if (!metadata.ok) {
        return;
    }
    int lastFrame = std::max(0, metadata.frameCount - 1);

    // If a right-eye video is present, constrain the range to the shorter video
    if (!j.right.empty()) {
        const CSlice::SliceVideoLoader::Metadata rightMetadata = CSlice::SliceVideoLoader::probeMetadata(j.right);
        if (rightMetadata.ok && rightMetadata.frameCount > 0) {
            lastFrame = std::min(lastFrame, std::max(0, rightMetadata.frameCount - 1));
        }
    }

    j.start = std::clamp(j.start, 0, lastFrame);
    if (j.stop <= 0) {
        j.stop = lastFrame;
    }
    else {
        j.stop = std::clamp(j.stop, 0, lastFrame);
    }
}

int frameIndexForRenderedFrame(const SliceJob &j, int renderedFrame)
{
    if (j.start == j.stop) {
        return j.start;
    }

    const int step = std::max(1, j.steps);
    const int direction = j.stop >= j.start ? 1 : -1;
    const int nextFrame = j.start + (renderedFrame * step * direction);
    return direction > 0 ? std::min(nextFrame, j.stop) : std::max(nextFrame, j.stop);
}

int loadedFrameCount()
{
    return sliceLoader ? std::min(sliceLoader->loadedFrameCount(), totalFrames) : renderedFrames;
}

void reportProgress(bool force = false)
{
    const int loadedFrames = loadedFrameCount();
    const int percent = totalFrames > 0
        ? std::clamp(static_cast<int>((100.0 * static_cast<double>(renderedFrames)) / static_cast<double>(totalFrames)), 0, 100)
        : 100;
    const double elapsed = sgct::time() - startTime;
    const bool changed = loadedFrames != lastReportedLoadedFrames || renderedFrames != lastReportedRenderedFrames;
    if (!force && !changed && lastProgressReportTime >= 0.0 && elapsed - lastProgressReportTime < 0.5) {
        return;
    }

    std::cout << std::format("Progress {} {} {} {} {} {:.1f}s\n",
        percent,
        currentFrameIndex,
        loadedFrames,
        renderedFrames,
        totalFrames,
        elapsed) << std::flush;
    lastReportedLoadedFrames = loadedFrames;
    lastReportedRenderedFrames = renderedFrames;
    lastProgressReportTime = elapsed;
}

SliceJob parseSliceArguments(const std::vector<std::string> &arguments)
{
    SliceJob parsed;

    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::string &arg = arguments[i];
        if (arg == "--config" && hasValue(arguments, i)) {
            parsed.config = arguments[++i];
        }
        else if (arg == "-input-type" && hasValue(arguments, i)) {
            parsed.inputType = inputTypeFromString(arguments[++i]);
        }
        else if (arg == "-left" && hasValue(arguments, i)) {
            parsed.left = arguments[++i];
        }
        else if (arg == "-right" && hasValue(arguments, i)) {
            parsed.right = arguments[++i];
        }
        else if (arg == "-start" && hasValue(arguments, i)) {
            parsed.start = toInt(arguments[++i], parsed.start);
        }
        else if (arg == "-stop" && hasValue(arguments, i)) {
            parsed.stop = toInt(arguments[++i], parsed.stop);
        }
        else if (arg == "-steps" && hasValue(arguments, i)) {
            parsed.steps = std::max(1, toInt(arguments[++i], parsed.steps));
        }
        else if (arg == "-max-encoder-load-threads" && hasValue(arguments, i)) {
            const int threadCount = toInt(arguments[++i], parsed.maxEncoderThreads);
            parsed.maxEncoderThreads = threadCount <= 0 ? 16 : std::clamp(threadCount, 1, 128);
        }
        else if (arg == "-image-delay-ms" && hasValue(arguments, i)) {
            parsed.imageDelayMs = std::max(0, toInt(arguments[++i], parsed.imageDelayMs));
        }
        else if (arg == "-image-buffering-threads" && hasValue(arguments, i)) {
            parsed.imageBufferingThreadCount = std::clamp(toInt(arguments[++i], parsed.imageBufferingThreadCount), 1, 64);
        }
        else if (arg == "-capture-gpu-slots" && hasValue(arguments, i)) {
            parsed.captureGpuSlots = std::clamp(toInt(arguments[++i], parsed.captureGpuSlots), 1, 128);
        }
        else if (arg == "-no-encode") {
            parsed.noEncode = true;
        }
        else if (arg == "-no-readback") {
            parsed.noReadback = true;
        }
        else if (arg == "-pixrate" && hasValue(arguments, i)) {
            parsed.pixrate = std::max(1, toInt(arguments[++i], parsed.pixrate));
        }
        else if (arg == "-constantquality" && hasValue(arguments, i)) {
            parsed.constantQuality = std::clamp(toInt(arguments[++i], parsed.constantQuality), 0, 51);
        }
        else if (arg == "-framerate" && i + 2 < arguments.size()) {
            parsed.frameRateNum = std::max(1, toInt(arguments[++i], parsed.frameRateNum));
            parsed.frameRateDen = std::max(1, toInt(arguments[++i], parsed.frameRateDen));
        }
        else if (arg == "-input-framerate-num" && hasValue(arguments, i)) {
            parsed.inputFrameRateNum = std::max(1, toInt(arguments[++i], parsed.inputFrameRateNum));
        }
        else if (arg == "-input-framerate-den" && hasValue(arguments, i)) {
            parsed.inputFrameRateDen = std::max(1, toInt(arguments[++i], parsed.inputFrameRateDen));
        }
        else if (arg == "-mapping" && hasValue(arguments, i)) {
            parsed.mappingMode = mappingModeFromString(arguments[++i]);
        }
        else if (arg == "-surface-radius" && hasValue(arguments, i)) {
            parsed.surfaceRadius = std::max(1.0, toDouble(arguments[++i], parsed.surfaceRadius));
        }
        else if (arg == "-surface-fov" && hasValue(arguments, i)) {
            parsed.surfaceFov = std::clamp(toDouble(arguments[++i], parsed.surfaceFov), 1.0, 360.0);
        }
        else if (arg == "-layer-stereo" && hasValue(arguments, i)) {
            parsed.layerStereoMode = stereoModeFromString(arguments[++i]);
        }
        else if (arg == "-layer-alpha" && hasValue(arguments, i)) {
            parsed.layerAlpha = std::clamp(toInt(arguments[++i], parsed.layerAlpha), 0, 100);
        }
        else if (arg == "-layer-roi" && i + 5 < arguments.size()) {
            parsed.layerRoiEnabled = toInt(arguments[++i], parsed.layerRoiEnabled ? 1 : 0) != 0;
            const float roiX = static_cast<float>(std::clamp(toDouble(arguments[++i], parsed.layerRoi.x), 0.0, 1.0));
            const float roiY = static_cast<float>(std::clamp(toDouble(arguments[++i], parsed.layerRoi.y), 0.0, 1.0));
            const float roiW = static_cast<float>(std::clamp(toDouble(arguments[++i], parsed.layerRoi.z), 0.001, 1.0));
            const float roiH = static_cast<float>(std::clamp(toDouble(arguments[++i], parsed.layerRoi.w), 0.001, 1.0));
            parsed.layerRoi = glm::vec4(std::min(roiX, 1.0f - roiW), std::min(roiY, 1.0f - roiH), roiW, roiH);
        }
        else if (arg == "-layer-rotate" && i + 3 < arguments.size()) {
            parsed.layerRotate.x = static_cast<float>(std::clamp(toDouble(arguments[++i], parsed.layerRotate.x), -360.0, 360.0));
            parsed.layerRotate.y = static_cast<float>(std::clamp(toDouble(arguments[++i], parsed.layerRotate.y), -360.0, 360.0));
            parsed.layerRotate.z = static_cast<float>(std::clamp(toDouble(arguments[++i], parsed.layerRotate.z), -360.0, 360.0));
        }
        else if (arg == "-plane-orientation" && i + 3 < arguments.size()) {
            parsed.planeAzimuth = std::clamp(toDouble(arguments[++i], parsed.planeAzimuth), -360.0, 360.0);
            parsed.planeElevation = std::clamp(toDouble(arguments[++i], parsed.planeElevation), -180.0, 180.0);
            parsed.planeRoll = std::clamp(toDouble(arguments[++i], parsed.planeRoll), -360.0, 360.0);
        }
        else if (arg == "-plane-position" && i + 3 < arguments.size()) {
            parsed.planeDistance = std::max(1.0, toDouble(arguments[++i], parsed.planeDistance));
            parsed.planeHorizontal = toDouble(arguments[++i], parsed.planeHorizontal);
            parsed.planeVertical = toDouble(arguments[++i], parsed.planeVertical);
        }
        else if (arg == "-plane-size" && i + 3 < arguments.size()) {
            parsed.planeWidth = std::max(0.0, toDouble(arguments[++i], parsed.planeWidth));
            parsed.planeHeight = std::max(0.0, toDouble(arguments[++i], parsed.planeHeight));
            parsed.planeAspectRatio = static_cast<std::uint8_t>(std::clamp(toInt(arguments[++i], parsed.planeAspectRatio), 0, 2));
        }
        else if (arg == "-codec" && hasValue(arguments, i)) {
            parsed.codec = arguments[++i];
        }
        else if (arg == "-preset" && hasValue(arguments, i)) {
            parsed.preset = arguments[++i];
        }
        else if (arg == "-libx-tune" && hasValue(arguments, i)) {
            parsed.libxTune = arguments[++i];
        }
        else if (arg == "-nvenc-tune" && hasValue(arguments, i)) {
            parsed.nvencTune = arguments[++i];
        }
        else if (arg == "-nvenc-hardware-frames") {
            parsed.nvencHardwareFrames = true;
        }
        else if (arg == "-use-only-iframes") {
            parsed.useOnlyIframes = true;
        }
        else if (arg == "-gop-size" && hasValue(arguments, i)) {
            parsed.gopSize = std::clamp(toInt(arguments[++i], parsed.gopSize), 1, 60);
        }
        else if (arg == "-encoding-bit-depth" && hasValue(arguments, i)) {
            parsed.encodingBitDepth = toInt(arguments[++i], parsed.encodingBitDepth) >= 10 ? 10 : 8;
        }
        else if (arg == "-parameterFile" && hasValue(arguments, i)) {
            parsed.parameterFile = arguments[++i];
        }
        else if (arg == "-image-error-behavior" && hasValue(arguments, i)) {
            parsed.imageErrorBehavior = imageErrorBehaviorFromString(arguments[++i]);
        }
        else if (arg == "--video-decoding-mode" && hasValue(arguments, i)) {
            parsed.videoDecodingMode = videoDecodingModeFromString(arguments[++i]);
        }
        else if (arg == "-image-size-warning-percent" && hasValue(arguments, i)) {
            parsed.imageSizeWarningPercent = std::clamp(toInt(arguments[++i], parsed.imageSizeWarningPercent), 0, 100);
        }
        else if (arg == "-verify-only") {
            parsed.verifyOnly = true;
            parsed.noEncode = true;
            parsed.noReadback = true;
        }
        else if (arg == "-out" && i + 2 < arguments.size()) {
            const std::string identifier = arguments[++i];
            const std::string output = arguments[++i];
            parsed.outputs.emplace_back(identifier, output);
        }
        else if (arg == "-upsidedown") {
            parsed.upsideDown = true;
        }
        else if (arg == "-warping") {
            parsed.warping = true;
        }
        else if (arg == "-blend-mask") {
            parsed.blendMask = true;
        }
    }

    return parsed;
}

std::vector<std::string> sgctArguments(const SliceJob &parsed)
{
    std::vector<std::string> arguments;
    if (parsed.config) {
        arguments.emplace_back("--config");
        arguments.emplace_back(*parsed.config);
    }
    return arguments;
}

std::string mappingModeString(MappingMode mode)
{
    switch (mode) {
        case MappingMode::SphereEqr: return "Sphere EQR";
        case MappingMode::SphereEac: return "Sphere EAC";
        case MappingMode::Plane: return "Plane";
        case MappingMode::Dome:
        default: return "Dome";
    }
}

bool clusterHasUser(const sgct::config::Cluster &cluster, std::string_view name)
{
    return std::any_of(cluster.users.begin(), cluster.users.end(), [name](const sgct::config::User &user) {
        return user.name.value_or(std::string("default")) == name;
    });
}

void ensureSafeViewportUsers(sgct::config::Cluster &cluster)
{
    if (cluster.users.empty()) {
        sgct::config::User defaultUser;
        defaultUser.name = "default";
        cluster.users.push_back(std::move(defaultUser));
    }

    for (sgct::config::Node &node : cluster.nodes) {
        for (sgct::config::Window &window : node.windows) {
            for (sgct::config::Viewport &viewport : window.viewports) {
                if (!viewport.user) {
                    continue;
                }

                if (viewport.user->empty() || !clusterHasUser(cluster, *viewport.user)) {
                    std::cerr << std::format(
                        "Viewport references unknown SGCT user '{}'; using default user instead.\n",
                        *viewport.user);
                    viewport.user.reset();
                }
            }
        }
    }
}

void applyNodeModeClusterDefaults(sgct::config::Cluster &cluster)
{
    sgct::config::Settings settings = cluster.settings.value_or(sgct::config::Settings{});
    sgct::config::Settings::Display display = settings.display.value_or(sgct::config::Settings::Display{});
    display.swapInterval = static_cast<std::int8_t>(0);
    settings.display = display;
    cluster.settings = settings;

    for (sgct::config::Node &node : cluster.nodes) {
        for (sgct::config::Window &window : node.windows) {
            window.alpha = false;
            window.takeScreenshot = false;
        }
    }
}

void keepOnlyRequestedOutputWindows(sgct::config::Cluster &cluster)
{
    if (job.outputs.empty()) {
        return;
    }

    int originalIndex = 0;
    int removedCount = 0;
    for (sgct::config::Node &node : cluster.nodes) {
        std::vector<sgct::config::Window> requestedWindows;
        requestedWindows.reserve(node.windows.size());
        for (sgct::config::Window &window : node.windows) {
            if (outputRequestedForWindow(window, originalIndex)) {
                requestedWindows.push_back(std::move(window));
            }
            else {
                ++removedCount;
            }
            ++originalIndex;
        }
        node.windows = std::move(requestedWindows);
    }

    if (removedCount > 0) {
        std::cout << std::format("Removed {} unselected SGCT output window(s) from the render configuration\n", removedCount) << std::flush;
    }
}

bool isTextureMappedProjection(const sgct::config::Viewport &viewport)
{
    return std::holds_alternative<sgct::config::TextureMappedProjection>(viewport.projection);
}

void configureCsliceWarping(sgct::config::Cluster &cluster)
{
    warpWindowConfigs.clear();

    for (sgct::config::Node &node : cluster.nodes) {
        for (sgct::config::Window &window : node.windows) {
            CSlice::SliceRenderer::WarpWindowConfig windowConfig;
            windowConfig.id = window.id;
            windowConfig.name = window.name.value_or(std::string());
            windowConfig.viewports.reserve(window.viewports.size());

            for (sgct::config::Viewport &viewport : window.viewports) {
                CSlice::SliceRenderer::WarpViewportConfig viewportConfig;
                if (job.warping) {
                    viewportConfig.correctionMesh = viewport.correctionMeshTexture.value_or(std::filesystem::path());
                    if (job.blendMask) {
                        viewportConfig.blendMask = viewport.blendMaskTexture.value_or(std::filesystem::path());
                        viewportConfig.blackLevelMask = viewport.blackLevelMaskTexture.value_or(std::filesystem::path());
                    }
                    viewportConfig.textureRenderMode = isTextureMappedProjection(viewport);
                    windowConfig.viewports.push_back(std::move(viewportConfig));
                }

                viewport.correctionMeshTexture.reset();
                viewport.blendMaskTexture.reset();
                viewport.blackLevelMaskTexture.reset();
            }

            if (job.warping) {
                warpWindowConfigs.push_back(std::move(windowConfig));
            }
        }
    }
}

void preWindow()
{
    for (const std::unique_ptr<sgct::Window> &window : sgct::Engine::instance().windows()) {
        window->setRenderWhileHidden(true);
        window->setFixResolution(true);
        window->setTakeScreenshot(false);
    }
}

void initOpenGL(GLFWwindow *)
{
    SliceRenderer = std::make_unique<CSlice::SliceRenderer>();
    SliceRenderer->configureWarping(warpWindowConfigs);
    SliceRenderer->initializeGL(job.surfaceRadius, job.surfaceFov);

    if (sliceLoader) {
        sliceLoader->initializeGL();
    }
    totalFrames = expectedFrameCount(job);
}

std::vector<std::byte> encode()
{
    std::vector<std::byte> data;
    sgct::serializeObject(data, currentFrameIndex);
    sgct::serializeObject(data, totalFrames);
    sgct::serializeObject(data, receivedTerminate);
    return data;
}

void decode(const std::vector<std::byte> &data)
{
    unsigned int pos = 0;
    sgct::deserializeObject(data, pos, currentFrameIndex);
    sgct::deserializeObject(data, pos, totalFrames);
    sgct::deserializeObject(data, pos, receivedTerminate);
}

void preSync()
{
    if (sgct::Engine::instance().isMaster() && receivedTerminate) {
        sgct::Engine::instance().terminate();
    }
}

void postSyncPreDraw()
{
    if (sliceLoader && loaderPauseApplied != pauseRequested.load()) {
        loaderPauseApplied = pauseRequested.load();
        sliceLoader->setPaused(loaderPauseApplied);
        if (!loaderPauseApplied) {
            currentFrameIndex = frameIndexForRenderedFrame(job, capturedFrames);
            sliceLoader->resetLoadingFromFrame(currentFrameIndex);
        }
    }

    if (receivedTerminate) {
        sgct::Engine::instance().terminate();
        return;
    }

    if (pauseRequested) {
        shouldCapture = false;
        reportProgress(false);
        return;
    }

    if (!sliceLoader || sliceLoader->empty()) {
        sgct::Engine::instance().terminate();
        return;
    }

    if (capturedFrames >= totalFrames && renderedFrames >= totalFrames) {
        std::cout << "All done\n" << std::flush;
        sgct::Engine::instance().terminate();
        return;
    }

    if (capturedFrames >= totalFrames) {
        shouldCapture = false;
        reportProgress(false);
        return;
    }

    currentFrameIndex = frameIndexForRenderedFrame(job, capturedFrames);
    sliceLoader->uploadPendingTextures(currentFrameIndex, true);

    const bool continueOnImageError = job.imageErrorBehavior == ImageErrorBehavior::Continue;
    const bool frameReady = sliceLoader->showFrame(currentFrameIndex, continueOnImageError);
    const int loadedFrameIndex = sliceLoader->currentFrameIndex();
    if (const std::optional<CSlice::SliceVerificationError> failure = sliceLoader->takeFirstFailure()) {
        reportCriticalError(std::format("Failed to read input {}: {}",
            job.inputType == SliceInputType::Video ? "video" : "image",
            failure->message), failure->path);
        if (job.imageErrorBehavior == ImageErrorBehavior::Pause) {
            pauseRequested = true;
            std::cout << "Paused\n" << std::flush;
        }
        else if (job.imageErrorBehavior != ImageErrorBehavior::Continue) {
            receivedTerminate = true;
            sgct::Engine::instance().terminate();
        }
    }

    if (!frameReady || (loadedFrameIndex > 0 && loadedFrameIndex != currentFrameIndex)) {
        shouldCapture = false;
        reportProgress(false);
        return;
    }

    if (movieOutput && !job.noReadback && movieReadbackQueueFull()) {
        shouldCapture = false;
        reportProgress(false);
        return;
    }

    shouldCapture = true;
}

void draw(const sgct::RenderData &data)
{
    if (!sliceLoader || !SliceRenderer) {
        return;
    }

    const bool rightEye = data.frustumMode == sgct::FrustumMode::StereoRight;
    const CSlice::SliceLayer* layer = sliceLoader->layerForEye(rightEye);
    if (!layer || layer->textureId == 0) {
        return;
    }

    const std::string &windowName = data.window.name();
    const int gridModeOverride = windowName.rfind("DomeMaster", 0) == 0
        ? static_cast<int>(CSlice::GridMode::None)
        : -1;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    if (job.warping) {
        SliceRenderer->renderLayerWithWarp(data, *layer, data.frustumMode, 0.0f, gridModeOverride);
    }
    else {
        SliceRenderer->renderLayer(data, *layer, data.frustumMode, 0.0f, gridModeOverride);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

bool captureTexture(sgct::Window &window, CSlice::SliceCaptureOutput &output)
{
    const GLuint texture = window.frameBufferTextureEye(sgct::Eye::MonoOrLeft);
    if (texture == 0) {
        return false;
    }

    const sgct::ivec2 resolution = window.framebufferResolution();
    if (resolution.x <= 0 || resolution.y <= 0) {
        return false;
    }

    output.setResolution(resolution);
    const size_t bytes = static_cast<size_t>(resolution.x) * static_cast<size_t>(resolution.y) * static_cast<size_t>(output.bytesPerPixel());
    if (output.pixels().size() != bytes) {
        output.pixels().resize(bytes);
    }

    window.makeOpenGLContextCurrent();
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, output.pixelFormat(), GL_UNSIGNED_BYTE, output.pixels().data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool verifySource(const CSlice::SliceSourceOptions &options)
{
    CSlice::SliceImageLoader::VerificationResult result = CSlice::SliceImageLoader::verifySource(options,
        reportVerificationProgress,
        const_cast<std::string *>(&options.identifier),
        job.imageSizeWarningPercent);
    for (const CSlice::SliceImageLoader::VerificationError &warning : result.warnings) {
        std::cout << std::format("VerificationWarning {} input: {} ({})\n",
            options.identifier,
            warning.message,
            warning.path.string()) << std::flush;
    }
    if (!result.ok) {
        for (const CSlice::SliceImageLoader::VerificationError &error : result.errors) {
            reportCriticalError(std::format("Failed to verify {} input: {}", options.identifier, error.message), error.path);
        }
        return false;
    }
    return true;
}

bool verifySliceInputs()
{
    if (job.inputType == SliceInputType::Video) {
        std::cout << "Video input verification is skipped.\n" << std::flush;
        return true;
    }
    verificationStartTime = std::chrono::steady_clock::now();
    bool ok = verifySource(sourceOptions(job.left, "left"));
    if (!job.right.empty()) {
        ok = verifySource(sourceOptions(job.right, "right")) && ok;
    }

    std::cout << "Verification complete\n" << std::flush;
    return ok;
}

bool pumpMovieOutputs(bool &submittedFrame, bool block)
{
    submittedFrame = false;
    const std::vector<std::unique_ptr<sgct::Window>> &windows = sgct::Engine::instance().windows();
    bool anySubmitted = false;
    for (std::size_t i = 0; i < windowOutputs.size() && i < windows.size(); ++i) {
        CSlice::SliceCaptureOutput &output = windowOutputs[i];
        if (!output.active()) {
            continue;
        }
        bool outputSubmitted = false;
        if (!output.pump(*windows[i], block, outputSubmitted)) {
            return false;
        }
        if (outputSubmitted) {
            anySubmitted = true;
        }
    }
    submittedFrame = anySubmitted;
    return true;
}

bool movieReadbackQueueFull()
{
    for (const CSlice::SliceCaptureOutput &output : windowOutputs) {
        if (output.active() && output.readbackQueueFull()) {
            return true;
        }
    }
    return false;
}

int encodedMovieFrameCount()
{
    int result = totalFrames;
    bool hasActiveOutput = false;
    for (const CSlice::SliceCaptureOutput &output : windowOutputs) {
        if (!output.active() || !output.hasEncoder()) {
            continue;
        }
        hasActiveOutput = true;
        result = std::min(result, output.encodedFrameCount());
    }
    return hasActiveOutput ? result : renderedFrames;
}

void postDraw()
{
    if (!sgct::Engine::instance().isMaster()) {
        return;
    }

    if (sliceLoader) {
        sliceLoader->uploadPendingTextures(currentFrameIndex, false);
    }

    bool fatalEncodingError = false;
    if (movieOutput) {
        while (true) {
            bool submittedFrame = false;
            if (!pumpMovieOutputs(submittedFrame, false)) {
                fatalEncodingError = true;
                break;
            }
            if (!submittedFrame) {
                break;
            }
            if (!job.noEncode) {
                renderedFrames = std::min(encodedMovieFrameCount(), totalFrames);
            }
        }
    }

    if (fatalEncodingError) {
        reportProgress(true);
        receivedTerminate = true;
        std::cout << "Done\n" << std::flush;
        sgct::Engine::instance().terminate();
        return;
    }

    if (!shouldCapture) {
        reportProgress(true);
        if (capturedFrames >= totalFrames && renderedFrames >= totalFrames) {
            std::cout << "Done\n" << std::flush;
            sgct::Engine::instance().terminate();
        }
        return;
    }

    if (job.noReadback) {
        if (movieOutput && !job.noEncode && isNvencCodec(job)) {
            std::cerr << "NVENC without OpenGL readback requires OpenGL/CUDA interop, which is not available in this build yet. The CUDA hardware-frame option only accelerates FFmpeg's NVENC input path after readback. Disable Run without readback or Run without encoding.\n";
            fatalEncodingError = true;
        }
        if (fatalEncodingError) {
            reportProgress(true);
            receivedTerminate = true;
            std::cout << "Done\n" << std::flush;
            sgct::Engine::instance().terminate();
            return;
        }
        if (sliceLoader) {
            sliceLoader->releaseFrame(currentFrameIndex);
        }
        capturedFrames = std::min(capturedFrames + 1, totalFrames);
        renderedFrames = std::min(capturedFrames, totalFrames);
        reportProgress(true);
        shouldCapture = false;
        if (capturedFrames >= totalFrames && renderedFrames >= totalFrames) {
            std::cout << "Done\n" << std::flush;
            sgct::Engine::instance().terminate();
        }
        return;
    }

    const std::vector<std::unique_ptr<sgct::Window>> &windows = sgct::Engine::instance().windows();
    while (!fatalEncodingError && movieOutput && movieReadbackQueueFull()) {
        bool submittedFrame = false;
        if (!pumpMovieOutputs(submittedFrame, true)) {
            fatalEncodingError = true;
            break;
        }
        if (!submittedFrame) {
            fatalEncodingError = true;
            break;
        }
        if (!job.noEncode) {
            renderedFrames = std::min(encodedMovieFrameCount(), totalFrames);
        }
    }

    int activeOutputCount = 0;
    int capturedOutputCount = 0;
    for (size_t i = 0; !fatalEncodingError && i < windows.size() && i < windowOutputs.size(); ++i) {
        CSlice::SliceCaptureOutput &output = windowOutputs[i];
        if (!output.active()) {
            continue;
        }
        ++activeOutputCount;

        if (movieOutput && !job.noReadback) {
            if (!output.blitReadback(*windows[i], currentFrameIndex, capturedFrames)) {
                std::cerr << std::format("Failed to blit and queue movie readback for window {} source frame {}\n", i, currentFrameIndex);
                fatalEncodingError = true;
                continue;
            }
        }
        else {
            if (!captureTexture(*windows[i], output)) {
                std::cerr << std::format("Failed to capture output window {} for source frame {}\n", i, currentFrameIndex);
                fatalEncodingError = true;
                continue;
            }

            const std::filesystem::path frameOutput = CSlice::numberedOutputPath(output.outputPath(), currentFrameIndex);
            if (frameOutput.has_parent_path()) {
                std::filesystem::create_directories(frameOutput.parent_path());
            }
            if (!CSlice::SliceEncoder::encodeStill(frameOutput,
                    desiredCodec,
                    output.resolution().x,
                    output.resolution().y,
                    output.pixels().data(),
                    job.parameterFile))
            {
                std::cerr << std::format("Failed to write image output '{}'\n", frameOutput.string());
                fatalEncodingError = true;
                continue;
            }
        }
        ++capturedOutputCount;
    }

    if (activeOutputCount == 0 || capturedOutputCount != activeOutputCount) {
        fatalEncodingError = true;
    }

    if (!fatalEncodingError) {
        if (sliceLoader) {
            sliceLoader->releaseFrame(currentFrameIndex);
        }
        capturedFrames = std::min(capturedFrames + 1, totalFrames);
        if (!movieOutput) {
            renderedFrames = capturedFrames;
        }
        else if (job.noEncode) {
            renderedFrames = std::min(renderedFrames + 1, totalFrames);
        }
    }
    reportProgress(true);

    shouldCapture = false;
    if (fatalEncodingError || (capturedFrames >= totalFrames && renderedFrames >= totalFrames)) {
        if (fatalEncodingError) {
            receivedTerminate = true;
        }
        std::cout << "Done\n" << std::flush;
        sgct::Engine::instance().terminate();
    }
}

void cleanup()
{
    const std::vector<std::unique_ptr<sgct::Window>> &windows = sgct::Engine::instance().windows();
    for (size_t i = 0; i < windowOutputs.size() && i < windows.size(); ++i) {
        if (movieOutput && windowOutputs[i].active() && !windowOutputs[i].finish(*windows[i])) {
            receivedTerminate = true;
        }
        windowOutputs[i].cleanup();
    }
    windowOutputs.clear();

    sliceLoader.reset();
    SliceRenderer.reset();

    const double elapsed = sgct::time() - startTime;
    std::cout << std::format("Exporting took {:.1f}s\n", elapsed) << std::flush;
}

void keyboard(sgct::Key key, sgct::Modifier, sgct::Action action, int, sgct::Window *)
{
    if (key == sgct::Key::Esc && action == sgct::Action::Press) {
        receivedTerminate = true;
    }
}

void externalDecode(const char *message, int size)
{
    const std::string_view payload(message, static_cast<size_t>(std::max(0, size)));
    if (payload.find("terminate") != std::string_view::npos) {
        receivedTerminate = true;
    }
}

void externalStatus(bool connected)
{
    externalConnected = connected;
    std::cout << (connected ? "External control connected\n" : "External control disconnected\n") << std::flush;
}

CSlice::SliceSourceOptions sourceOptions(const std::string &path,
                                                 std::string identifier,
                                                 bool validateSequenceFrames)
{
    CSlice::SliceSourceOptions options;
    options.input = path;
    options.identifier = std::move(identifier);
    options.start = job.start;
    options.stop = job.stop;
    options.step = job.steps;
    options.imageDelayMs = job.imageDelayMs;
    options.videoDecodingMode = videoDecodingModeString(job.videoDecodingMode);
    options.validateSequenceFrames = validateSequenceFrames;
    return options;
}

CSlice::SliceRenderOptions renderOptions()
{
    CSlice::SliceRenderOptions options;
    options.gridMode = gridModeFromMappingMode(job.mappingMode);
    options.stereoMode = job.layerStereoMode;
    options.alpha = 1.0f;
    options.alpha = static_cast<float>(job.layerAlpha) / 100.f;
    options.flipY = !job.upsideDown;
    options.rotate = job.layerRotate;
    options.roiEnabled = job.layerRoiEnabled;
    options.roi = job.layerRoi;
    options.planeAzimuth = job.planeAzimuth;
    options.planeElevation = job.planeElevation;
    options.planeRoll = job.planeRoll;
    options.planeDistance = job.planeDistance;
    options.planeHorizontal = job.planeHorizontal;
    options.planeVertical = job.planeVertical;
    options.planeWidth = job.planeWidth > 0.0 ? job.planeWidth : job.surfaceRadius * 2.0;
    options.planeHeight = job.planeHeight > 0.0 ? job.planeHeight : job.surfaceRadius;
    options.planeAspectRatio = job.planeAspectRatio;
    return options;
}

bool prepareSliceLoader()
{
    if (job.inputType == SliceInputType::Video) {
        sliceLoader = std::make_unique<CSlice::SliceVideoLoader>();
    }
    else {
        sliceLoader = std::make_unique<CSlice::SliceImageLoader>();
    }
    sliceLoader->configureBudgets(job.imageBufferingThreadCount);
    const CSlice::SliceRenderOptions layerRenderOptions = renderOptions();

    std::string error;
    if (!sliceLoader->addSource(sourceOptions(job.left, "left", false),
            layerRenderOptions,
            &error)) {
        std::cerr << std::format("Failed to prepare left/input layer: {}\n", error);
        return false;
    }

    if (!job.right.empty()) {
        error.clear();
        if (!sliceLoader->addSource(sourceOptions(job.right, "right", false),
                layerRenderOptions,
                &error)) {
            std::cerr << std::format("Failed to prepare right/input layer: {}\n", error);
            return false;
        }
    }

    sliceLoader->startLoading();
    std::cout << std::format("{} loader using {} worker thread-owned texture slot(s), requested {}\n",
        inputTypeString(job.inputType),
        sliceLoader->effectiveLoadingThreadCount(),
        job.imageBufferingThreadCount) << std::flush;
    return true;
}

void initializeEncoders()
{
    const std::vector<std::unique_ptr<sgct::Window>> &windows = sgct::Engine::instance().windows();
    windowOutputs.clear();
    windowOutputs.resize(windows.size());

    for (const std::pair<std::string, std::string> &outputSpec : job.outputs) {
        const std::string &identifier = outputSpec.first;
        const std::string &outputPath = outputSpec.second;
        if (identifier.empty() || outputPath.empty()) {
            continue;
        }

        std::optional<size_t> outputIndex;
        if (isUnsignedInteger(identifier)) {
            const int index = toInt(identifier, -1);
            if (index >= 0 && static_cast<size_t>(index) < windows.size()) {
                outputIndex = static_cast<size_t>(index);
            }
            for (size_t i = 0; i < windows.size(); ++i) {
                if (windows[i]->id() == index) {
                    outputIndex = i;
                    break;
                }
            }
        }
        else {
            for (size_t i = 0; i < windows.size(); ++i) {
                if (windows[i]->name() == identifier) {
                    outputIndex = i;
                    break;
                }
            }
        }

        if (!outputIndex) {
            std::cerr << std::format("No SGCT window output matches identifier '{}'.\n", identifier);
            continue;
        }

        CSlice::SliceCaptureOutput& output = windowOutputs[*outputIndex];
        output.configure(
            outputPath,
            GL_BGR,
            3,
            job.captureGpuSlots
        );
        if (output.outputPath().has_parent_path()) {
            std::filesystem::create_directories(output.outputPath().parent_path());
        }

        if (!movieOutput) {
            continue;
        }
        if (job.noEncode) {
            continue;
        }

        CSlice::SliceCaptureOutput::EncoderOptions options;
        options.codec = desiredCodec;
        options.hardwareEncoderName = hardwareEncoderName(job.codec);
        options.frameRateNum = job.frameRateNum;
        options.frameRateDen = job.frameRateDen;
        options.inputFrameRateNum = job.inputFrameRateNum;
        options.inputFrameRateDen = job.inputFrameRateDen;
        options.constantQuality = job.constantQuality;
        options.preset = job.preset;
        options.libxTune = job.libxTune;
        options.nvencTune = job.nvencTune;
        options.nvencHardwareFrames = job.nvencHardwareFrames;
        options.encodingBitDepth = job.encodingBitDepth;
        options.parameterFile = job.parameterFile;
        options.useOnlyIframes = (job.useOnlyIframes);
        options.gopSize = job.gopSize;
        options.pixrate = job.pixrate;
        options.maxEncoderThreads = static_cast<unsigned int>(job.maxEncoderThreads);

        if (!output.initializeMovie(*windows[*outputIndex], options)) {
            std::cerr << std::format(
                "Failed to initialize encoder for window '{}' output '{}'\n",
                identifier,
                output.outputPath().string()
            );
            output.setActive(false);
        }
        else if (options.hardwareEncoderName == "h264_nvenc" || options.hardwareEncoderName == "hevc_nvenc") {
            if (output.usingNvencHardwareFrames()) {
                std::cout << std::format("NVENC output '{}' using CUDA hardware-frame pipeline. OpenGL readback is still required in this build.\n", identifier) << std::flush;
            }
            else if (output.nvencHardwareFramesRequested()) {
                std::cout << std::format("NVENC output '{}' could not use CUDA hardware-frame pipeline; falling back to software-frame NVENC input with OpenGL readback.\n", identifier) << std::flush;
            }
            else {
                std::cout << std::format("NVENC output '{}' using software-frame NVENC input with OpenGL readback.\n", identifier) << std::flush;
            }
        }
    }
}

} // namespace

bool CSlice::Node::isNodeMode(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--node" || arg == "-left" || arg == "-right" || arg == "-out") {
            return true;
        }
    }
    return false;
}

int CSlice::Node::run(int argc, char **argv)
{
    std::vector<std::string> rawArguments(argv + 1, argv + argc);
    rawArguments.erase(std::remove(rawArguments.begin(), rawArguments.end(), "--node"), rawArguments.end());

    job = parseSliceArguments(rawArguments);
    normalizeVideoFrameRange(job);
    currentFrameIndex = job.start;
    totalFrames = expectedFrameCount(job);
    capturedFrames = 0;
    renderedFrames = 0;
    lastReportedLoadedFrames = -1;
    lastReportedRenderedFrames = -1;
    lastProgressReportTime = -1.0;
    shouldCapture = false;
    receivedTerminate = false;
    externalConnected = false;
    pauseRequested = false;
    loaderPauseApplied = false;
    desiredCodec = CSlice::codecIdFromName(job.codec);
    movieOutput = CSlice::isMovieCodec(desiredCodec);

    std::cout << "C-Slice node mode\n";
    std::cout << "FFmpeg " << CSlice::FFmpegProbe::versionString().toStdString() << "\n";
    std::cout << "Mapping: " << mappingModeString(job.mappingMode)
              << ", radius: " << job.surfaceRadius << " cm"
              << ", FOV: " << job.surfaceFov << " deg"
              << ", codec: " << job.codec << "\n";
    std::cout << "Input type: " << inputTypeString(job.inputType) << "\n";
    std::cout << "Layers: left " << (job.inputType == SliceInputType::Video ? "Video" : "Image");
    if (!job.right.empty()) {
        std::cout << ", right " << (job.inputType == SliceInputType::Video ? "Video" : "Image");
    }
    std::cout << ", loading threads " << job.imageBufferingThreadCount
              << ", capture GPU slots " << job.captureGpuSlots
              << "\n";
    std::cout << "Frames: " << job.start << " -> " << job.stop << " step " << job.steps << "\n";

    if (job.verifyOnly) {
        return verifySliceInputs() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    startCommandInputThread();

    std::vector<std::string> sgctArgs = sgctArguments(job);
    sgct::Configuration config = sgct::parseArguments(sgctArgs);
    if (config.showHelpText.value_or(false)) {
        std::cout << sgct::helpMessage() << "\n";
        return EXIT_SUCCESS;
    }
    if (!config.configFilename) {
        std::cerr << "C-Slice node mode requires an SGCT JSON configuration via --config or -config.\n";
        return EXIT_FAILURE;
    }

    sgct::config::Cluster cluster;
    try {
        cluster = sgct::loadCluster(config.configFilename);
    }
    catch (const std::runtime_error &e) {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }
    if (!cluster.success) {
        std::cerr << "Failed to load SGCT configuration: " << *config.configFilename << "\n";
        return EXIT_FAILURE;
    }
    keepOnlyRequestedOutputWindows(cluster);
    ensureSafeViewportUsers(cluster);
    applyNodeModeClusterDefaults(cluster);
    configureCsliceWarping(cluster);

    sgct::Engine::Callbacks callbacks;
    callbacks.preWindow = preWindow;
    callbacks.initOpenGL = initOpenGL;
    callbacks.preSync = preSync;
    callbacks.postSyncPreDraw = postSyncPreDraw;
    callbacks.draw = draw;
    callbacks.postDraw = postDraw;
    callbacks.cleanup = cleanup;
    callbacks.encode = encode;
    callbacks.decode = decode;
    callbacks.keyboard = keyboard;
    callbacks.externalDecode = externalDecode;
    callbacks.externalStatus = externalStatus;

    try {
        sgct::Engine::create(cluster, callbacks, config);
        sgct::Engine::instance().setCaptureFromBackBuffer(false);

        if (!prepareSliceLoader()) {
            std::cerr << "Left/input media is required.\n";
            return EXIT_FAILURE;
        }

        // Re-clamp totalFrames to the loader's actual minimum across all sources.
        // This matters for stereo video when the right clip is shorter than the left.
        if (sliceLoader) {
            const int loaderTotal = sliceLoader->totalFrameCount();
            if (loaderTotal > 0) {
                totalFrames = std::min(totalFrames, loaderTotal);
            }
        }

        initializeEncoders();
    }
    catch (const std::runtime_error &e) {
        sgct::Log::Error(e.what());
        sgct::Engine::destroy();
        return EXIT_FAILURE;
    }

    startTime = sgct::time();
    sgct::Engine::instance().exec();
    sgct::Engine::destroy();
    return receivedTerminate ? EXIT_FAILURE : EXIT_SUCCESS;
}
