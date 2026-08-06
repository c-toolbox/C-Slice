/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sliceapplication.h"

#include "ffmpegprobe.h"
#include "jobqueue/slicequeuemodel.h"
#include "mpvpreviewitem.h"
#include "slicesettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QIODevice>
#include <QModelIndex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <KAboutData>
#include <KColorSchemeManager>
#include <KLocalizedString>

namespace {

QString defaultSliceDataPath(const QString &relativePath)
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

} // namespace

static QGuiApplication *createApplication(int &argc, char **argv)
{
    Q_INIT_RESOURCE(images);

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication::setOrganizationName(QStringLiteral("org.ctoolbox.cslice"));
    QGuiApplication::setApplicationName(QStringLiteral("C-Slice"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("c-toolbox.github.io/C-Slice"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("C-Slice ") + QString::fromLatin1(CSLICE_VERSION));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(CSLICE_VERSION));
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication *app = new QGuiApplication(argc, argv);
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    QIcon::setThemeName(QStringLiteral("breeze"));
    app->setWindowIcon(QIcon(QStringLiteral(":/C_transparent.png")));
    return app;
}

SliceApplication::SliceApplication(int &argc, char **argv)
    : m_app(createApplication(argc, argv))
{
    m_aboutData = new KAboutData(QStringLiteral("cslice"),
        QStringLiteral("C-Slice"),
        QString::fromLatin1(CSLICE_VERSION));
    m_aboutData->setShortDescription(QStringLiteral("A slicing utility for immersive images and SGCT cluster output."));
    m_aboutData->setLicense(KAboutLicense::GPL_V3);
    m_aboutData->setCopyrightStatement(QString::fromUtf8("(c) 2026 Erik Sundén"));
    m_aboutData->setHomepage(QStringLiteral("https://c-toolbox.github.io/C-Slice/"));
    m_aboutData->setBugAddress(QStringLiteral("https://github.com/c-toolbox/C-Slice/issues").toUtf8());
    m_aboutData->setDesktopFileName(QStringLiteral("org.ctoolbox.cslice"));
    m_aboutData->addAuthor(QString::fromUtf8("Contact/owner: Erik Sundén"),
        QStringLiteral("Creator of C-Slice"),
        QStringLiteral("eriksunden85@gmail.com"));
    KAboutData::setApplicationData(*m_aboutData);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("C-Slice ") + QString::fromLatin1(CSLICE_VERSION));

    KColorSchemeManager *schemes = KColorSchemeManager::instance();
    schemes->activateScheme(schemes->indexForScheme(QStringLiteral("Breeze Dark")));

    m_engine = new QQmlApplicationEngine(m_app);
    QObject::connect(m_engine, &QQmlApplicationEngine::quit, m_app, &QCoreApplication::quit);
    QQmlEngine::setObjectOwnership(m_app, QQmlEngine::CppOwnership);
    qmlRegisterType<CSlice::MpvPreviewItem>("org.ctoolbox.cslice", 1, 0, "MpvPreviewItem");
    qmlRegisterSingletonInstance("org.ctoolbox.cslice", 1, 0, "SliceSettings", SliceSettings::self());
    m_engine->rootContext()->setContextProperty(QStringLiteral("app"), this);

    // Create internal queue model for drag-and-drop reordering
    SliceQueueModel *internalQueueModel = new SliceQueueModel(this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("internalQueueModel"), internalQueueModel);

    // Sync controller's internalQueuedJobs to the model.
    // Using queued connection ensures setJobNames() is called AFTER any in-flight
    // model operations (like beginMoveRows/endMoveRows) complete, preventing conflicts.
    connect(&m_controller, &SliceController::internalQueueChanged, [this, internalQueueModel]() {
        QMetaObject::invokeMethod(internalQueueModel, [this, internalQueueModel]() {
            QStringList jobs = m_controller.property("internalQueuedJobs").toStringList();
            internalQueueModel->setJobNames(jobs);
        }, Qt::QueuedConnection);
    });

    // When model emits rowsMoved, forward to controller to physically reorder the queue.
    // Using queued connection ensures this happens asynchronously, avoiding conflicts
    // with any in-flight Qt model operations (beginMoveRows/endMoveRows).
    connect(internalQueueModel, &SliceQueueModel::rowsMoved, [this](int start, int /*end*/, int destination) {
        QMetaObject::invokeMethod(&m_controller, [this, start, destination]() {
            m_controller.reorderInternalQueue(start, destination);
        }, Qt::QueuedConnection);
    });

    const QUrl moduleUrl(QStringLiteral("qrc:/qt/qml/org/ctoolbox/CSlice/Main.qml"));
    QObject::connect(m_engine, &QQmlApplicationEngine::objectCreated,
        m_app,
        [moduleUrl](QObject *object, const QUrl &objectUrl) {
            if (!object && objectUrl == moduleUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    m_engine->loadFromModule("org.ctoolbox.cslice", "Main");
}

SliceApplication::~SliceApplication()
{
    delete m_engine;
    delete m_aboutData;
    delete m_app;
}

int SliceApplication::run()
{
    return m_app->exec();
}

SliceController *SliceApplication::controller()
{
    return &m_controller;
}

QString SliceApplication::ffmpegVersion() const
{
    return CSlice::FFmpegProbe::versionString();
}

QString SliceApplication::ffmpegLibraries() const
{
    return CSlice::FFmpegProbe::libraryString();
}

QString SliceApplication::version() const
{
    return QString::fromLatin1(CSLICE_VERSION);
}

QUrl SliceApplication::pathToUrl(const QString &path) const
{
    return QUrl::fromLocalFile(QDir::fromNativeSeparators(path));
}

QString SliceApplication::urlToPath(const QUrl &url) const
{
    return QDir::toNativeSeparators(url.toLocalFile());
}

QString SliceApplication::sliceDataPath(const QString &relativePath) const
{
    return defaultSliceDataPath(relativePath);
}

QString SliceApplication::readTextFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}
