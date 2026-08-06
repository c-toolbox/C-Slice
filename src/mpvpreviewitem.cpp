/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mpvpreviewitem.h"

#include "mpvvideo.h"

#include <QDebug>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QQuickWindow>
#include <QQuickGraphicsDevice>
#include <QTimer>

#include <array>
#include <cmath>
#include <filesystem>

namespace CSlice {

namespace {

std::filesystem::path pathFromQString(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path(reinterpret_cast<const char8_t*>(path.toUtf8().constData()));
#else
    return std::filesystem::path(path.toStdString());
#endif
}

} // namespace

// Render-thread helper that owns the MpvVideo and draws its texture into the
// Qt Quick scene graph. All methods run on the Qt Quick render thread with the
// scene graph's OpenGL context current.
//
// Key design principle: mpv video loading (load()) does NOT require an OpenGL
// context, but GL resource creation (initializeGL()) DOES. To avoid access
// violations when the preview window is first shown, we separate these two steps:
// 1. Video loading happens eagerly in setSource() (no GL needed)
// 2. GL initialization happens at the end of init(), which runs on
//    beforeRendering where the GL context IS guaranteed to be current.
//
// Timing:
//   Frame N: window opens, beforeSynchronizing -> sync() creates renderer,
//            setSource() loads video (no GL). beforeRendering -> init() sets up
//            shaders but no video yet. beforeRenderPassRecording -> paint() runs.
//   Frame N+1: beforeRendering -> init() finds pending video and does GL init.
class MpvPreviewRenderer : public QObject, protected QOpenGLFunctions {
    Q_OBJECT

public:
    MpvPreviewRenderer() = default;

    ~MpvPreviewRenderer() override
    {
        m_video.reset();
        delete m_program;
    }

    void setWindow(QQuickWindow *window) { m_window = window; }
    void setWindowSize(const QSize &size) { m_windowSize = size; }
    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setItemPosition(const QPoint &position) { m_itemPosition = position; }
    void setItemVisible(bool visible) { m_itemVisible = visible; }

    void setSource(const QString &source)
    {
        if (m_pendingSource == source) {
            return;
        }
        m_pendingSource = source;
        m_sourceDirty = true;
        // The actual load + GL init is deferred to init(), which runs on the Qt
        // Quick render thread where the OpenGL context is guaranteed current.
    }

    void setFrame(int frame)
    {
        if (m_pendingFrame == frame) {
            return;
        }
        m_pendingFrame = frame;
        m_frameDirty = true;
    }

public Q_SLOTS:
    void init()
    {
        // Shader/VAO/VBO initialization - runs once.
        if (!m_program) {
            QSGRendererInterface* rif = m_window->rendererInterface();
            Q_ASSERT(rif->graphicsApi() == QSGRendererInterface::OpenGL);

            QOpenGLContext* ctx = QOpenGLContext::currentContext();
            if (!ctx)
                return;

            // Point the private window at the same OpenGL context that is current on
            // the parent window's render thread. This guarantees that any GL objects
            // (textures, FBOs) created inside update() share the same namespace as
            // LayerQtOpenGLObject and LayersRendererQtOpenGLObject, which are also
            // driven by beforeRendering of the same parent window.
            m_window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(ctx));

            initializeOpenGLFunctions();

            m_program = new QOpenGLShaderProgram();
            m_program->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex,
                "attribute highp vec3 vertices;"
                "attribute highp vec2 texcoords;"
                "varying highp vec2 coords;"
                "uniform bool flipY;"
                "void main() {"
                "    gl_Position = vec4(vertices, 1.0);"
                "    coords = flipY ? vec2(texcoords.x, 1.0 - texcoords.y) : texcoords;"
                "}");
            m_program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment,
                "varying highp vec2 coords;"
                "uniform sampler2D tex;"
                "void main() {"
                "    gl_FragColor = texture2D(tex, coords);"
                "}");
            m_program->bindAttributeLocation("vertices", 0);
            m_program->bindAttributeLocation("texcoords", 1);
            m_program->link();

            constexpr std::array<float, 20> quad = {
                // x     y     z     u    v
                -1.f, -1.f, 0.f, 0.f, 0.f,
                 1.f, -1.f, 0.f, 1.f, 0.f,
                -1.f,  1.f, 0.f, 0.f, 1.f,
                 1.f,  1.f, 0.f, 1.f, 1.f
            };

            m_vao.create();
            m_vao.bind();
            m_vbo.create();
            m_vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
            m_vbo.bind();
            m_vbo.allocate(quad.data(), static_cast<int>(quad.size() * sizeof(float)));

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                reinterpret_cast<void*>(3 * sizeof(float)));

            m_vbo.release();
            m_vao.release();
        }

        std::string error;
        if (!m_video) {
            m_video = std::make_unique<MpvVideo>();
            if (!m_video->initializeMpv(&error)) {
                qWarning() << "MpvPreviewRenderer: failed to initialize Mpv for video:"
                    << QString::fromStdString(error);
                return;
            }
            // Disable advanced control so mpv's internal GL state management doesn't
            // conflict with Qt Quick's OpenGL context.
            m_video->setSoftwareRendering(true);
            // The Qt Quick render thread has no GLAD-loaded GL functions, so MpvVideo
            // must not make direct GL calls. We own the texture/FBO here using
            // QOpenGLFunctions and supply them through setRenderTarget().
            m_video->setExternalTextureOwnership(true);
        }

        if (!m_glInitialized) {
            if (!QOpenGLContext::currentContext()) {
                return;
            }
            if (!m_video->initializeGL(&error)) {
                qWarning() << "MpvPreviewRenderer: failed to initialize GL for video:"
                    << QString::fromStdString(error);
                return;
            }
            m_glInitialized = true;
        }
    }

    void paint()
    {
        if (!m_window || !m_program || !m_itemVisible) {
            return;
        }

        m_window->beginExternalCommands();

        // We are on the render thread with a current OpenGL context. (Re)load the
        // video when the requested source changed. Both mpv load and GL resource
        // creation happen here where the context is guaranteed current.
        if (m_sourceDirty) {
            m_sourceDirty = false;
            if (m_window) {
                loadPendingVideo();
            }
        }

        updateMedia();

        if (!m_video || !m_video->valid()) {
            m_window->endExternalCommands();
            return;
        }

        // Ensure the externally-owned render target matches mpv's current output
        // size. We own these GL resources via QOpenGLFunctions because the Qt
        // Quick render thread has no GLAD-loaded GL functions.
        int outWidth = 0;
        int outHeight = 0;
        if (m_video->currentOutputSize(outWidth, outHeight)) {
            ensureRenderTarget(outWidth, outHeight);
        }
        else if (m_extTex == 0) {
            ensureRenderTarget(m_video->videoWidth(), m_video->videoHeight());
        }

        if (m_video->needsRender()) {
            m_video->render();
        }

        if (!m_video->textureReady() || m_video->textureId() == 0) {
            m_window->endExternalCommands();
            return;
        }

        const QRect viewport = aspectFitViewport();
        if (viewport.width() <= 0 || viewport.height() <= 0) {
            m_window->endExternalCommands();
            return;
        }

        m_program->bind();
        m_program->enableAttributeArray(0);
        m_program->enableAttributeArray(1);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_video->textureId());
        m_program->setUniformValue("tex", 0);
        m_program->setUniformValue("flipY", false);

        glViewport(viewport.x(), viewport.y(), viewport.width(), viewport.height());

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_vao.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_vao.release();

        glDisable(GL_BLEND);

        m_program->disableAttributeArray(0);
        m_program->disableAttributeArray(1);
        m_program->release();

        glViewport(0, 0, m_windowSize.width(), m_windowSize.height());

        m_window->endExternalCommands();
    }

    void cleanup()
    {
        m_video.reset();
        // Release the render target we own (MpvVideo never deletes external resources).
        if (m_extFbo != 0) {
            glDeleteFramebuffers(1, &m_extFbo);
            m_extFbo = 0;
        }
        if (m_extTex != 0) {
            glDeleteTextures(1, &m_extTex);
            m_extTex = 0;
        }
        m_targetWidth = 0;
        m_targetHeight = 0;
        delete m_program;
        m_program = nullptr;
        if (m_vao.isCreated()) {
            m_vao.destroy();
        }
        if (m_vbo.isCreated()) {
            m_vbo.destroy();
        }
    }

    // (Re)load the video for the current source and initialize its GL resources.
    // Must be called from the render thread where the OpenGL context is current,
    // so the load and GL init happen atomically within the same frame.
    void loadPendingVideo()
    {
        m_loadedSource = m_pendingSource;
        m_lastTime = -1.0;
        m_frameDirty = true;

        if (m_loadedSource.isEmpty()) {
            return;
        }

        std::string error;

        if (!m_video) {
            m_video = std::make_unique<MpvVideo>();
            if (!m_video->initializeMpv(&error)) {
                qWarning() << "MpvPreviewRenderer: failed to initialize Mpv for video:"
                    << QString::fromStdString(error);
                return;
            }
            // Disable advanced control so mpv's internal GL state management doesn't
            // conflict with Qt Quick's OpenGL context.
            m_video->setSoftwareRendering(true);
        }

        if (!m_glInitialized) {
            if (!m_video->initializeGL(&error)) {
                qWarning() << "MpvPreviewRenderer: failed to initialize GL for video:"
                    << QString::fromStdString(error);
                return;
            }
            m_glInitialized = true;
        }

        if (!m_video->load(pathFromQString(m_loadedSource), &error)) {
            qWarning() << "MpvPreviewRenderer: failed to load video:" << m_loadedSource
                       << QString::fromStdString(error);
            return;
        }
    }

private:
    // Creates or resizes the externally-owned texture + framebuffer that mpv
    // renders into, using QOpenGLFunctions. Mirrors MpvVideo's internal format
    // (GL_RGBA16F, linear filtering, clamp-to-edge). Supplies them to the video
    // via setRenderTarget().
    void ensureRenderTarget(int width, int height)
    {
        width = std::max(1, width);
        height = std::max(1, height);
        if (m_extTex != 0 && width == m_targetWidth && height == m_targetHeight) {
            return;
        }

        if (m_extTex == 0) {
            glGenTextures(1, &m_extTex);
        }
        glBindTexture(GL_TEXTURE_2D, m_extTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (m_extFbo == 0) {
            glGenFramebuffers(1, &m_extFbo);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_extFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_extTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        m_targetWidth = width;
        m_targetHeight = height;

        if (m_video) {
            m_video->setRenderTarget(m_extTex, m_extFbo, width, height);
        }
    }

    void updateMedia()
    {
        if (m_video && m_video->valid() && m_frameDirty) {
            m_frameDirty = false;
            const double fps = m_video->fps() > 0.0 ? m_video->fps() : 1.0;
            const double time = m_pendingFrame / fps;
            if (std::abs(time - m_lastTime) > 1e-6) {
                m_lastTime = time;
                m_video->setTimePosition(time);
            }
        }
    }

    QRect aspectFitViewport() const
    {
        const int videoWidth = m_video->textureWidth() > 0 ? m_video->textureWidth() : m_video->videoWidth();
        const int videoHeight = m_video->textureHeight() > 0 ? m_video->textureHeight() : m_video->videoHeight();
        if (videoWidth <= 0 || videoHeight <= 0 ||
            m_viewportSize.width() <= 0 || m_viewportSize.height() <= 0) {
            return QRect();
        }

        const double videoAspect = static_cast<double>(videoWidth) / videoHeight;
        const double itemAspect = static_cast<double>(m_viewportSize.width()) / m_viewportSize.height();

        int fitWidth = m_viewportSize.width();
        int fitHeight = m_viewportSize.height();
        if (videoAspect > itemAspect) {
            fitHeight = static_cast<int>(std::round(m_viewportSize.width() / videoAspect));
        }
        else {
            fitWidth = static_cast<int>(std::round(m_viewportSize.height() * videoAspect));
        }

        const int offsetX = m_itemPosition.x() + (m_viewportSize.width() - fitWidth) / 2;
        const int offsetYTop = m_itemPosition.y() + (m_viewportSize.height() - fitHeight) / 2;
        // Convert from top-left (Qt) to bottom-left (OpenGL) origin.
        const int offsetYBottom = m_windowSize.height() - offsetYTop - fitHeight;

        return QRect(offsetX, offsetYBottom, fitWidth, fitHeight);
    }

    QQuickWindow *m_window = nullptr;
    QSize m_windowSize;
    QSize m_viewportSize;
    QPoint m_itemPosition;
    bool m_itemVisible = true;

    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;

    // Video with GL resources initialized.
    std::unique_ptr<MpvVideo> m_video;
    bool m_glInitialized = false;
    // Externally-owned render target supplied to m_video (we own these here).
    unsigned int m_extTex = 0;
    unsigned int m_extFbo = 0;
    int m_targetWidth = 0;
    int m_targetHeight = 0;
    QString m_pendingSource;
    QString m_loadedSource;
    int m_pendingFrame = 0;
    double m_lastTime = -1.0;
    bool m_sourceDirty = false;
    bool m_frameDirty = false;
};

MpvPreviewItem::MpvPreviewItem()
{
    connect(this, &QQuickItem::windowChanged, this, &MpvPreviewItem::handleWindowChanged);
}

MpvPreviewItem::~MpvPreviewItem() = default;

QString MpvPreviewItem::source() const { return m_source; }

void MpvPreviewItem::setSource(const QString &source)
{
    if (m_source == source) {
        return;
    }
    m_source = source;
    Q_EMIT sourceChanged();
    if (window()) {
        window()->update();
    }
}

int MpvPreviewItem::frame() const { return m_frame; }

void MpvPreviewItem::setFrame(int frame)
{
    if (m_frame == frame) {
        return;
    }
    m_frame = frame;
    Q_EMIT frameChanged();
    if (window()) {
        window()->update();
    }
}

void MpvPreviewItem::handleWindowChanged(QQuickWindow *win)
{
    if (!win) {
        return;
    }
    connect(win, &QQuickWindow::beforeSynchronizing, this, &MpvPreviewItem::sync, Qt::DirectConnection);
    connect(win, &QQuickWindow::sceneGraphInvalidated, this, &MpvPreviewItem::cleanup, Qt::DirectConnection);

    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setInterval(static_cast<int>((1.0 / 60.0) * 1000.0));
        connect(m_timer, &QTimer::timeout, win, &QQuickWindow::update);
        m_timer->start();
    }
}

void MpvPreviewItem::sync()
{
    if (!window()) {
        return;
    }
    if (!m_renderer) {
        m_renderer = new MpvPreviewRenderer();
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &MpvPreviewRenderer::init, Qt::DirectConnection);
        connect(window(), &QQuickWindow::beforeRenderPassRecording, m_renderer, &MpvPreviewRenderer::paint, Qt::DirectConnection);
    }

    const qreal dpr = window()->devicePixelRatio();
    m_renderer->setWindow(window());
    m_renderer->setWindowSize(window()->size() * dpr);
    m_renderer->setViewportSize((this->size() * dpr).toSize());
    m_renderer->setItemPosition((this->mapToScene(QPointF(0, 0)) * dpr).toPoint());
    m_renderer->setItemVisible(isVisible());
    m_renderer->setSource(m_source);
    m_renderer->setFrame(m_frame);
}

void MpvPreviewItem::cleanup()
{
    if (m_renderer) {
        m_renderer->cleanup();
        delete m_renderer;
        m_renderer = nullptr;
    }
}

void MpvPreviewItem::releaseResources()
{
    cleanup();
}

} // namespace CSlice

#include "mpvpreviewitem.moc"
