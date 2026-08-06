/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mpvvideo.h"
#include "ffmpegprobe.h"

#include <sgct/opengl.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>

namespace CSlice {

namespace {

std::string mpvErrorString(int error)
{
    return mpv_error_string(error);
}

bool command(mpv_handle *handle, std::initializer_list<const char*> arguments, std::string *errorMessage = nullptr)
{
    std::vector<const char*> values(arguments);
    values.push_back(nullptr);
    const int result = mpv_command(handle, values.data());
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = mpvErrorString(result);
        }
        return false;
    }
    return true;
}

bool setOptionString(mpv_handle *handle, const char *name, const char *value, std::string *errorMessage = nullptr)
{
    const int result = mpv_set_option_string(handle, name, value);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = std::format("Failed to set mpv option {}: {}", name, mpvErrorString(result));
        }
        return false;
    }
    return true;
}

bool getPropertyDouble(mpv_handle *handle, const char *name, double &value)
{
    double property = 0.0;
    const int result = mpv_get_property(handle, name, MPV_FORMAT_DOUBLE, &property);
    if (result < 0 || !std::isfinite(property)) {
        return false;
    }
    value = property;
    return true;
}

bool getPropertyInt64(mpv_handle *handle, const char *name, std::int64_t &value)
{
    std::int64_t property = 0;
    const int result = mpv_get_property(handle, name, MPV_FORMAT_INT64, &property);
    if (result < 0) {
        return false;
    }
    value = property;
    return true;
}

std::string pathToUtf8(const std::filesystem::path &path)
{
#ifdef _WIN32
    const auto text = std::filesystem::path(path).u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
#else
    return path.string();
#endif
}

void waitForFileLoaded(mpv_handle *handle)
{
    while (true) {
        mpv_event *event = mpv_wait_event(handle, 5.0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            return;
        }
        if (event->event_id == MPV_EVENT_FILE_LOADED || event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_SHUTDOWN) {
            return;
        }
    }
}

} // namespace

MpvVideo::~MpvVideo()
{
    cleanup();
}

void MpvVideo::setHybridRightEye(bool rightEye)
{
    // Only applies when in Hybrid mode
    if (m_decodingMode != DecodingMode::Hybrid) {
        return;
    }
    // Right eye uses software rendering, left eye uses hardware decoding
    m_softwareRendering = rightEye;
    m_advancedControl = m_softwareRendering ? 0 : 1;
    if (m_handle) {
        mpv_set_option_string(m_handle, "hwdec", m_softwareRendering ? "no" : "auto");
    }
}

void MpvVideo::setDecodingMode(DecodingMode mode)
{
    m_decodingMode = mode;
    // Determine software rendering based on decoding mode
    switch (mode) {
    case DecodingMode::Software:
        m_softwareRendering = true;
        break;
    case DecodingMode::Hardware:
        m_softwareRendering = false;
        break;
    case DecodingMode::Hybrid:
        // Hybrid default (before source type is known): use software rendering
        m_softwareRendering = true;
        break;
    }
    m_advancedControl = m_softwareRendering ? 0 : 1;
    if (m_handle) {
        mpv_set_option_string(m_handle, "hwdec", m_softwareRendering ? "no" : "auto");
    }
}

bool MpvVideo::initializeMpv(std::string* errorMessage)
{
    if (m_handle) {
        return true;
    }

    m_handle = mpv_create();
    if (!m_handle) {
        if (errorMessage) {
            *errorMessage = "Failed to create mpv handle.";
        }
        return false;
    }

    std::string optionError;
    setOptionString(m_handle, "vo", "libmpv", &optionError);
    setOptionString(m_handle, "pause", "yes", &optionError);
    setOptionString(m_handle, "idle", "yes", &optionError);
    setOptionString(m_handle, "audio", "no", &optionError);
    setDecodingMode(m_decodingMode);

    const int result = mpv_initialize(m_handle);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = std::format("Failed to initialize mpv: {}", mpvErrorString(result));
        }
        return false;
    }
    return true;
}

void MpvVideo::setSoftwareRendering(bool enabled)
{
    m_softwareRendering = enabled;
    m_advancedControl = enabled ? 0 : 1;
    // Apply hwdec at runtime too, since this may be called after initializeMpv()
    // but before a file is loaded.
    if (m_handle) {
        mpv_set_option_string(m_handle, "hwdec", enabled ? "no" : "auto");
    }
}

bool MpvVideo::initializeGL(std::string* errorMessage)
{
    if (m_glInitialized) {
        return true;
    }
    if (!m_handle) {
        if (errorMessage) {
            *errorMessage = "mpv handle is not initialized.";
        }
        return false;
    }

    // Start with a placeholder texture/FBO; the real size is established on the
    // first render once the video dimensions are known. When the texture/FBO are
    // owned externally, the owner allocates them and supplies them through
    // setRenderTarget(), so we skip all direct GL calls here.
    if (!m_externalTextureOwnership) {
        const int initialWidth = m_videoWidth > 0 ? m_videoWidth : 1;
        const int initialHeight = m_videoHeight > 0 ? m_videoHeight : 1;
        createTextureAndFbo(initialWidth, initialHeight);
    }

    if (!createRenderContext(errorMessage)) {
        return false;
    }

    m_glInitialized = true;
    return true;
}

bool MpvVideo::load(const std::filesystem::path &input, std::string *errorMessage)
{
    if (!m_handle) {
        initializeMpv(errorMessage);
        if (!m_handle) {
            return false;
        }
    }

    if (!m_glInitialized) {
        initializeGL(errorMessage);
        if (!m_glInitialized) {
            return false;
        }
    }

    const std::string inputPath = pathToUtf8(input);
    std::string commandError;
    if (!command(m_handle, { "loadfile", inputPath.c_str() }, &commandError)) {
        if (errorMessage) {
            *errorMessage = std::format("Failed to load video {}: {}", inputPath, commandError);
        }
        return false;
    }
    waitForFileLoaded(m_handle);

    readMetadata(input);
    return true;
}

void MpvVideo::readMetadata(const std::filesystem::path &input)
{
    std::int64_t width = 0;
    std::int64_t height = 0;
    double duration = 0.0;
    double fps = 0.0;
    double estimatedFrames = 0.0;
    getPropertyInt64(m_handle, "width", width);
    getPropertyInt64(m_handle, "height", height);
    getPropertyDouble(m_handle, "duration", duration);
    getPropertyDouble(m_handle, "container-fps", fps);
    if (fps <= 0.0) {
        getPropertyDouble(m_handle, "estimated-vf-fps", fps);
    }
    getPropertyDouble(m_handle, "estimated-frame-count", estimatedFrames);

    m_videoWidth = static_cast<int>(std::max<std::int64_t>(0, width));
    m_videoHeight = static_cast<int>(std::max<std::int64_t>(0, height));
    m_duration = std::max(0.0, duration);
    m_fps = fps > 0.0 ? fps : 1.0;
    const int accurateFrames = FFmpegProbe::accurateFrameCount(input);
    m_frameCount = accurateFrames > 0
        ? accurateFrames
        : (estimatedFrames > 0.0
            ? static_cast<int>(std::ceil(estimatedFrames))
            : static_cast<int>(std::ceil(m_duration * m_fps)));
    m_frameCount = std::max(1, m_frameCount);
}

void MpvVideo::setRenderTarget(unsigned int textureId, unsigned int framebufferId, int width, int height)
{
    m_textureId = textureId;
    m_framebufferId = framebufferId;
    m_fboWidth = std::max(0, width);
    m_fboHeight = std::max(0, height);
}

bool MpvVideo::currentOutputSize(int &width, int &height) const
{
    if (!m_handle) {
        return false;
    }
    std::int64_t w = 0;
    std::int64_t h = 0;
    if (getPropertyInt64(m_handle, "dwidth", w) &&
        getPropertyInt64(m_handle, "dheight", h) &&
        w > 0 && h > 0) {
        width = static_cast<int>(w);
        height = static_cast<int>(h);
        return true;
    }
    if (getPropertyInt64(m_handle, "width", w) &&
        getPropertyInt64(m_handle, "height", h) &&
        w > 0 && h > 0) {
        width = static_cast<int>(w);
        height = static_cast<int>(h);
        return true;
    }
    return false;
}

bool MpvVideo::createRenderContext(std::string *errorMessage)
{
    if (m_renderContext) {
        return true;
    }

    mpv_opengl_init_params glInitParams { getProcAddress };
    int advancedControl = m_advancedControl;
    mpv_render_param renderParams[] = {
        { MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL) },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };
    const int result = mpv_render_context_create(&m_renderContext, m_handle, renderParams);
    if (result < 0) {
        if (errorMessage) {
            *errorMessage = std::format("Failed to create mpv OpenGL render context: {}", mpvErrorString(result));
        }
        return false;
    }

    mpv_render_context_set_update_callback(m_renderContext, onRenderUpdate, this);
    return true;
}

void MpvVideo::createTextureAndFbo(int width, int height)
{
    if (m_externalTextureOwnership) {
        return;
    }

    width = std::max(1, width);
    height = std::max(1, height);

    if (m_textureId != 0) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (m_framebufferId != 0) {
        glDeleteFramebuffers(1, &m_framebufferId);
        m_framebufferId = 0;
    }
    glGenFramebuffers(1, &m_framebufferId);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebufferId);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textureId, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_fboWidth = width;
    m_fboHeight = height;
}

void MpvVideo::resizeFbo()
{
    if (m_externalTextureOwnership) {
        return;
    }

    if (m_fboWidth <= 0 || m_fboHeight <= 0) {
        return;
    }

    int maxTexSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    if (m_fboWidth > maxTexSize || m_fboHeight > maxTexSize) {
        return;
    }

    createTextureAndFbo(m_fboWidth, m_fboHeight);
}

void MpvVideo::render()
{
    if (!m_renderContext || m_framebufferId == 0) {
        return;
    }

    // Establish or update the FBO dimensions from the current video output. When
    // the render target is owned externally, the owner is responsible for
    // (re)allocating the texture/FBO and calling setRenderTarget(), so we never
    // touch GL state for sizing here.
    if (!m_externalTextureOwnership) {
        std::int64_t currentWidth = 0;
        std::int64_t currentHeight = 0;
        if (getPropertyInt64(m_handle, "dwidth", currentWidth) &&
            getPropertyInt64(m_handle, "dheight", currentHeight) &&
            currentWidth > 0 && currentHeight > 0) {
            if (static_cast<int>(currentWidth) != m_fboWidth || static_cast<int>(currentHeight) != m_fboHeight) {
                m_fboWidth = static_cast<int>(currentWidth);
                m_fboHeight = static_cast<int>(currentHeight);
                resizeFbo();
            }
        }
        else if (m_fboWidth <= 1 || m_fboHeight <= 1) {
            // Fall back to raw width/height if display dimensions are not ready yet.
            if (getPropertyInt64(m_handle, "width", currentWidth) &&
                getPropertyInt64(m_handle, "height", currentHeight) &&
                currentWidth > 0 && currentHeight > 0) {
                m_fboWidth = static_cast<int>(currentWidth);
                m_fboHeight = static_cast<int>(currentHeight);
                resizeFbo();
            }
        }
    }

    const int renderWidth = m_fboWidth > 0 ? m_fboWidth : m_videoWidth;
    const int renderHeight = m_fboHeight > 0 ? m_fboHeight : m_videoHeight;
    if (renderWidth <= 0 || renderHeight <= 0) {
        return;
    }

    mpv_opengl_fbo fbo { static_cast<int>(m_framebufferId), renderWidth, renderHeight, GL_RGBA16F };
    int flipY = m_flipY ? 0 : 1;
    mpv_render_param renderParams[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
        { MPV_RENDER_PARAM_FLIP_Y, &flipY },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };
    mpv_render_context_render(m_renderContext, renderParams);

    m_textureReady = true;
    m_needsRender = false;
}

void MpvVideo::setTimePosition(double seconds)
{
    if (!m_handle) {
        return;
    }
    double timestamp = std::max(0.0, seconds);
    mpv_set_property(m_handle, "time-pos", MPV_FORMAT_DOUBLE, &timestamp);
    m_needsRender = true;
}

void MpvVideo::frameStep()
{
    if (!m_handle) {
        return;
    }
    command(m_handle, { "frame-step" });
    m_needsRender = true;
}

void MpvVideo::setPaused(bool paused)
{
    if (!m_handle) {
        return;
    }
    int flag = paused ? 1 : 0;
    mpv_set_property(m_handle, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvVideo::cleanup()
{
    // Stop the event loop thread first (before destroying mpv handle)
    stopEventLoop();

    if (m_renderContext) {
        mpv_render_context_free(m_renderContext);
        m_renderContext = nullptr;
    }
    // Externally-owned texture/FBO are released by their owner.
    if (!m_externalTextureOwnership) {
        if (m_framebufferId != 0) {
            glDeleteFramebuffers(1, &m_framebufferId);
        }
        if (m_textureId != 0) {
            glDeleteTextures(1, &m_textureId);
        }
    }
    m_framebufferId = 0;
    m_textureId = 0;
    if (m_handle) {
        mpv_terminate_destroy(m_handle);
        m_handle = nullptr;
    }
    m_fboWidth = 0;
    m_fboHeight = 0;
    m_glInitialized = false;
    m_textureReady = false;
}

void* MpvVideo::getProcAddress(void*, const char *name)
{
#ifdef _WIN32
    static HMODULE opengl = LoadLibraryA("opengl32.dll");
    if (opengl) {
        if (void *address = reinterpret_cast<void*>(wglGetProcAddress(name))) {
            return address;
        }
        return reinterpret_cast<void*>(GetProcAddress(opengl, name));
    }
#endif
    return nullptr;
}

void MpvVideo::onRenderUpdate(void *context)
{
    if (!context) {
        return;
    }
    static_cast<MpvVideo*>(context)->m_needsRender = true;
}

// ---- Event loop thread implementation ----

void MpvVideo::startEventLoop()
{
    if (m_eventThreadRunning || !m_handle) {
        return;
    }

    m_eventThreadTerminate = false;
    m_eventThreadRunning = true;
    m_eventThread = std::make_unique<std::thread>(&MpvVideo::eventLoopWorker, this);
}

void MpvVideo::stopEventLoop()
{
    if (!m_eventThreadRunning) {
        return;
    }

    m_eventThreadTerminate = true;

    // Wake up mpv_wait_event by sending a dummy command
    if (m_handle) {
        mpv_command_string(m_handle, "no-op");
    }

    if (m_eventThread && m_eventThread->joinable()) {
        m_eventThread->join();
    }
    m_eventThread.reset();
    m_eventThreadRunning = false;
}

void MpvVideo::eventLoopWorker()
{
    while (!m_eventThreadTerminate) {
        processEvents();
        if (m_eventThreadTerminate) {
            break;
        }
        // Small sleep to avoid busy-waiting when no events are available.
        // mpv_wait_event with timeout 0 returns immediately if no events,
        // so we use a small sleep only when MPV_EVENT_NONE is received.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    m_eventThreadRunning = false;
}

void MpvVideo::processEvents()
{
    if (!m_handle) {
        return;
    }

    // Process all available mpv events
    while (true) {
        mpv_event* event = mpv_wait_event(m_handle, 0.01); // 10ms timeout to allow periodic termination check

        if (event->event_id == MPV_EVENT_NONE) {
            break;
        }

        handleEvent(event);
    }

    // Process any queued async commands from the main thread
    std::lock_guard<std::mutex> lock(m_commandMutex);
    while (!m_asyncCommands.empty()) {
        auto& [cmdType, cmdValue] = m_asyncCommands.front();
        if (cmdType == "frame-step") {
            mpv_command_string(m_handle, "frame-step");
        } else if (cmdType == "seek") {
            mpv_set_property(m_handle, "time-pos", MPV_FORMAT_DOUBLE, &cmdValue);
        }
        m_asyncCommands.pop();
    }
}

void MpvVideo::handleEvent(mpv_event *event)
{
    if (!event) {
        return;
    }

    switch (event->event_id) {
    case MPV_EVENT_FILE_LOADED: {
        // File successfully loaded, metadata will be read by the caller
        m_needsRender = true;
        break;
    }

    case MPV_EVENT_VIDEO_RECONFIG: {
        // Video dimensions may have changed
        std::int64_t w = 0, h = 0;
        if (mpv_get_property(m_handle, "dwidth", MPV_FORMAT_INT64, &w) >= 0 &&
            mpv_get_property(m_handle, "dheight", MPV_FORMAT_INT64, &h) >= 0 &&
            w > 0 && h > 0) {
            m_fboWidth = static_cast<int>(w);
            m_fboHeight = static_cast<int>(h);
            m_needsRender = true;
        }
        break;
    }

    case MPV_EVENT_PROPERTY_CHANGE: {
        mpv_event_property* prop = reinterpret_cast<mpv_event_property*>(event->data);
        if (!prop) {
            break;
        }

        if (strcmp(prop->name, "video-params") == 0 && prop->format == MPV_FORMAT_NODE) {
            // Video parameters changed (resolution, colorspace, etc.)
            m_needsRender = true;
        } else if (strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            m_duration = *reinterpret_cast<double*>(prop->data);
        } else if (strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            // Current playback position changed
            m_needsRender = true;
        }
        break;
    }

    case MPV_EVENT_END_FILE: {
        m_needsRender = true;
        break;
    }

    case MPV_EVENT_SHUTDOWN: {
        // mpv has shut down
        m_eventThreadTerminate = true;
        break;
    }

    case MPV_EVENT_LOG_MESSAGE: {
        mpv_event_log_message* msg = reinterpret_cast<mpv_event_log_message*>(event->data);
        if (msg) {
            // Forward mpv log messages based on severity
            if (msg->log_level >= MPV_LOG_LEVEL_FATAL) {
                // fprintf(stderr, "mpv FATAL: %s\n", msg->text);
            } else if (msg->log_level >= MPV_LOG_LEVEL_ERROR) {
                // fprintf(stderr, "mpv ERROR: %s\n", msg->text);
            }
        }
        break;
    }

    default: {
        // Ignore uninteresting events (idle, command_reply, etc.)
        break;
    }
    }
}

bool MpvVideo::advanceNextFrameAsync()
{
    if (!m_handle || !m_eventThreadRunning) {
        return false;
    }

    // Queue frame-step command for the event loop thread to execute
    std::lock_guard<std::mutex> lock(m_commandMutex);
    m_asyncCommands.push({"frame-step", 0.0});
    return true;
}

void MpvVideo::queueTimeSeek(double seconds)
{
    if (!m_handle || !m_eventThreadRunning) {
        return;
    }

    // Queue time seek command for the event loop thread to execute
    std::lock_guard<std::mutex> lock(m_commandMutex);
    m_asyncCommands.push({"seek", seconds});
}

} // namespace CSlice
