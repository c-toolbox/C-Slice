/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_MPVVIDEO_H
#define CSLICE_MPVVIDEO_H

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace CSlice {

// Self-contained libmpv render-to-texture pipeline.
//
// Each MpvVideo owns exactly one mpv handle, one OpenGL render context and one
// FBO/texture pair, so several instances can run independently without
// interfering with each other (e.g. left eye, right eye and the preview window).
//
// Threading: load() does not require an OpenGL context and may run on any
// thread. initializeGL(), render() and cleanup() must be called with the
// OpenGL context that owns the texture current.
class MpvVideo {
public:
    MpvVideo() = default;
    ~MpvVideo();

    MpvVideo(const MpvVideo&) = delete;
    MpvVideo& operator=(const MpvVideo&) = delete;

    // Create the mpv handle, initialize it. Returns false and fills errorMessage on failure.
    bool initializeMpv(std::string* errorMessage = nullptr);

    // Create the texture, FBO and mpv OpenGL render context. Requires a current
    // OpenGL context. Returns false and fills errorMessage on failure.
    bool initializeGL(std::string* errorMessage = nullptr);

    // Load the given file. Blocks until
    // the file is loaded (or fails). Returns false and fills errorMessage on
    // failure.
    bool load(const std::filesystem::path &input, std::string *errorMessage = nullptr);

    // Release all mpv and OpenGL resources. Requires a current OpenGL context.
    void cleanup();

    // Render the latest decoded frame into the internal FBO/texture. Resizes the
    // FBO to match the current video dimensions when needed. Requires a current
    // OpenGL context.
    void render();

    // Playback / seek controls.
    void setTimePosition(double seconds);
    void frameStep();
    void setPaused(bool paused);

    // Mark that mpv signalled a new frame is available (set from the render
    // update callback; also settable by callers that change the time position).
    void requestRender() { m_needsRender = true; }
    bool needsRender() const { return m_needsRender; }
    void clearNeedsRender() { m_needsRender = false; }

    // Sets the FLIP_Y value passed to mpv when rendering (true flips vertically).
    void setFlipY(bool flipY) { m_flipY = flipY; }

    enum class DecodingMode { Software, Hardware, Hybrid };

    // Sets the decoding mode. This controls software rendering and hwdec settings.
    // Must be called before initializeMpv() for the setting to take effect.
    void setDecodingMode(DecodingMode mode);
    DecodingMode decodingMode() const { return m_decodingMode; }

    // For Hybrid mode: sets whether this instance should use software rendering.
    // Call after setDecodingMode(Hybrid). rightEye=true means software rendering (right eye),
    // rightEye=false means hardware decoding (left eye).
    void setHybridRightEye(bool rightEye);

    // Enables software rendering mode for mpv. This avoids hardware decoding
    // and advanced OpenGL control, which can conflict with external GL contexts
    // (e.g., the Qt Quick render thread context). When enabled it disables
    // hwdec: hardware-decoded surfaces mapped into a foreign GL context produce
    // progressively corrupt frames as the decode surface pool cycles.
    void setSoftwareRendering(bool enabled);

    // When external ownership is enabled, MpvVideo makes no direct OpenGL calls
    // for its texture/FBO. Instead the owner allocates them (using whatever GL
    // function loader it has, e.g. QOpenGLFunctions) and supplies them through
    // setRenderTarget(). This is required when MpvVideo is used outside a GLAD
    // context. Must be set before initializeGL().
    void setExternalTextureOwnership(bool enabled) { m_externalTextureOwnership = enabled; }
    bool externalTextureOwnership() const { return m_externalTextureOwnership; }

    // Supplies the externally-owned texture, framebuffer and their size. Only
    // used when external ownership is enabled. Makes no OpenGL calls.
    void setRenderTarget(unsigned int textureId, unsigned int framebufferId, int width, int height);

    // Reads mpv's current decoded output size (dwidth/dheight, falling back to
    // width/height). Makes no OpenGL calls. Returns false if unavailable. Owners
    // use this to decide when to (re)allocate the external render target.
    bool currentOutputSize(int &width, int &height) const;

    // Starts/stops the background event processing thread. The event loop runs
    // mpv_wait_event continuously so that property changes and decode progress
    // are tracked without blocking the main rendering/slicing thread. Call this
    // after initializeMpv() but before any playback commands.
    void startEventLoop();
    void stopEventLoop();

    // Issues a frame-step command to mpv on the event loop thread for prefetching
    // the next frame. This allows decoding to happen in parallel with other
    // operations (slicing, encoding). Returns false if mpv handle is unavailable.
    bool advanceNextFrameAsync();

    // Queues a time-position seek asynchronously. Useful for seeking ahead to
    // the next frame's timestamp while the current frame is being processed.
    void queueTimeSeek(double seconds);

    // Accessors.
    mpv_handle* handle() const { return m_handle; }
    unsigned int textureId() const { return m_textureId; }
    int textureWidth() const { return m_fboWidth; }
    int textureHeight() const { return m_fboHeight; }
    bool textureReady() const { return m_textureReady; }
    bool valid() const { return m_handle != nullptr; }

    double fps() const { return m_fps; }
    double duration() const { return m_duration; }
    int frameCount() const { return m_frameCount; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }

private:
    static void* getProcAddress(void *context, const char *name);
    static void onRenderUpdate(void *context);

    // Event loop thread functions
    void eventLoopWorker();
    void processEvents();
    void handleEvent(mpv_event *event);

    bool createRenderContext(std::string *errorMessage);
    void createTextureAndFbo(int width, int height);
    void resizeFbo();
    void readMetadata(const std::filesystem::path &input);

    mpv_handle *m_handle = nullptr;
    mpv_render_context *m_renderContext = nullptr;
    unsigned int m_textureId = 0;
    unsigned int m_framebufferId = 0;
    int m_fboWidth = 0;
    int m_fboHeight = 0;
    int m_advancedControl = 1;
    bool m_softwareRendering = true;
    bool m_flipY = false;
    bool m_glInitialized = false;
    bool m_textureReady = false;
    bool m_externalTextureOwnership = false;
    DecodingMode m_decodingMode = DecodingMode::Software;
    std::atomic<bool> m_needsRender{true};

    // Event loop thread members
    std::unique_ptr<std::thread> m_eventThread;
    std::atomic<bool> m_eventThreadRunning{false};
    std::atomic<bool> m_eventThreadTerminate{false};
    std::mutex m_commandMutex;  // Protects async command queue
    std::queue<std::pair<std::string, double>> m_asyncCommands;  // {type, value}

    double m_fps = 0.0;
    double m_duration = 0.0;
    int m_frameCount = 0;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
};

} // namespace CSlice

#endif // CSLICE_MPVVIDEO_H
