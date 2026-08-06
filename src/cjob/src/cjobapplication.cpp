/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cjobapplication.h"
#include "jobqueue/cjobserver.h"
#include "jobqueue/jobqueuemodel.h"

#include <KColorSchemeManager>

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QModelIndex>
#include <QQuickStyle>
#include <QQuickWindow>
#include <qqml.h>

namespace {

QString defaultStitchDataPath(const QString &relativePath)
{
    const QString dataPath = QStringLiteral("data/") + QDir::fromNativeSeparators(relativePath);
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList searchRoots = {
        applicationDir,
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

QGuiApplication *createApplication(int &argc, char **argv)
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication::setOrganizationName(QStringLiteral("Visualization Center C"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("github.com/c-toolbox/C-Job"));
    QGuiApplication::setApplicationName(QStringLiteral("C-Job"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("C-Job 1.0.0"));
    QGuiApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication *app = new QGuiApplication(argc, argv);
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    QIcon::setThemeName(QStringLiteral("breeze"));
    app->setWindowIcon(QIcon(QStringLiteral(":/C_transparent.png")));

    KColorSchemeManager *schemes = KColorSchemeManager::instance();
    schemes->activateScheme(schemes->indexForScheme(QStringLiteral("Breeze Dark")));

    return app;
}

} // namespace

CJobApplication::CJobApplication(int &argc, char **argv)
{
    // Check for existing instance BEFORE creating QGuiApplication
    // This avoids unnecessary resource allocation if another instance is running
    if (CJobServer::isAnotherInstanceRunning(QStringLiteral("C-Job"))) {
        qCritical() << "Another C-Job instance is already running. Only one instance is allowed.";
        m_app = nullptr;
        m_engine = nullptr;
        m_server = nullptr;
        return;
    }

    m_app = createApplication(argc, argv);
    m_server = new CJobServer(this);

    // Connect server signals for debugging
    connect(m_server, &CJobServer::serverStarted, []() {
        qInfo() << "C-Job server started signal emitted";
    });
    connect(m_server, &CJobServer::serverStopped, []() {
        qInfo() << "C-Job server stopped signal emitted";
    });

    if (!m_server->startListening()) {
        qCritical() << "Failed to start C-Job server - another instance may be running";
        m_app = nullptr;
        m_engine = nullptr;
        return;
    }
    qInfo() << "C-Job server listening on:" << m_server->serverName();

    m_engine = new QQmlApplicationEngine(m_app);
    QObject::connect(m_engine, &QQmlApplicationEngine::quit, m_app, &QCoreApplication::quit);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);

    m_engine->rootContext()->setContextProperty(QStringLiteral("app"), this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("cjobServer"), m_server);
    
    // Create job queue model for drag-and-drop reordering
    JobQueueModel *jobQueueModel = new JobQueueModel(this);
    
    // Sync server jobs to the model using queued connection to avoid conflicts with moveRow() signals.
    // When QML calls model.moveRow(), it emits rowsMoved which triggers server reorder via queued connection.
    // The server then syncs back via setJobNames() also through queued connection, ensuring all
    // Qt model operations (beginMoveRows/endMoveRows vs beginResetModel/endResetModel) are serialized.
    connect(m_server, &CJobServer::jobsChanged, [this, jobQueueModel]() {
        QMetaObject::invokeMethod(jobQueueModel, [this, jobQueueModel]() {
            jobQueueModel->setJobNames(m_server->jobNames());
        }, Qt::QueuedConnection);
    });
    
    // Handle drag-and-drop reordering from the model.
    // When QML calls model.moveRow(), it emits rowsMoved which triggers server reorder via queued connection.
    // This ensures the physical re-order happens in the backend (CJobServer) asynchronously,
    // avoiding conflicts with any in-flight Qt model operations.
    connect(jobQueueModel, &JobQueueModel::rowsMoved, [this](int start, int /*end*/, int destination) {
        QMetaObject::invokeMethod(m_server, [this, start, destination]() {
            m_server->reorderJobQueue(start, destination);
        }, Qt::QueuedConnection);
    });
    
    m_engine->rootContext()->setContextProperty(QStringLiteral("jobQueueModel"), jobQueueModel);

    const QUrl moduleUrl(QStringLiteral("qrc:/qt/qml/org/ctoolbox/cjob/CJobMain.qml"));
    QObject::connect(m_engine, &QQmlApplicationEngine::objectCreated,
        m_app,
        [moduleUrl](QObject *object, const QUrl &objectUrl) {
            if (!object && objectUrl == moduleUrl) {
                qCritical() << "Failed to create C-Job main window";
                QCoreApplication::exit(-1);
            }
            if (object && !objectUrl.isEmpty()) {
                qDebug() << "QML object created:" << objectUrl.toString();
            }
        },
        Qt::QueuedConnection);

    qDebug() << "Loading CJobMain.qml...";
    m_engine->loadFromModule("org.ctoolbox.cjob", "CJobMain");
}

CJobApplication::~CJobApplication()
{
    // Safe cleanup - pointers may be nullptr if initialization failed
    delete m_server;
    delete m_engine;
    delete m_app;
}

int CJobApplication::run()
{
    if (!m_app) {
        return 1;  // Failed to initialize (another instance running)
    }
    return m_app->exec();
}

CJobServer *CJobApplication::server()
{
    return m_server;
}

QString CJobApplication::version() const
{
    return QStringLiteral("1.0.0");
}