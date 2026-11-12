/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "copyapplication.h"
#include "copycontroller.h"

#include <KColorSchemeManager>

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QModelIndex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

namespace {

const QString kInstanceServerName = QStringLiteral("C-Opy");
const QString kVersion = QStringLiteral("1.0.0");

QGuiApplication *createApplication(int &argc, char **argv)
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication::setOrganizationName(QStringLiteral("Visualization Center C"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("github.com/c-toolbox/C-Slice"));
    QGuiApplication::setApplicationName(QStringLiteral("C-Opy"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("C-Opy ") + kVersion);
    QGuiApplication::setApplicationVersion(kVersion);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));

    QGuiApplication *app = new QGuiApplication(argc, argv);
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    QIcon::setThemeName(QStringLiteral("breeze"));
    app->setWindowIcon(QIcon(QStringLiteral(":/data/images/C_transparent.png")));

    KColorSchemeManager *schemes = KColorSchemeManager::instance();
    schemes->activateScheme(schemes->indexForScheme(QStringLiteral("Breeze Dark")));

    return app;
}

} // namespace

COpyApplication::COpyApplication(int &argc, char **argv)
{
    m_app = createApplication(argc, argv);

    // Manifests, files and folders are given as plain arguments: C-Opy.exe job.c-opy.json [D:\Media\show ...]
    QStringList manifestPaths;
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        manifestPaths << QFileInfo(arguments.at(i)).absoluteFilePath();
    }

    if (forwardToRunningInstance(manifestPaths)) {
        m_forwarded = true;
        return;
    }

    m_controller = new COpyController(this);
    startInstanceServer();

    m_engine = new QQmlApplicationEngine(m_app);
    QObject::connect(m_engine, &QQmlApplicationEngine::quit, m_app, &QCoreApplication::quit);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(m_controller, QQmlEngine::CppOwnership);

    m_engine->rootContext()->setContextProperty(QStringLiteral("app"), this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("copyController"), m_controller);

    const QUrl moduleUrl(QStringLiteral("qrc:/qt/qml/org/ctoolbox/copy/COpyMain.qml"));
    QObject::connect(m_engine, &QQmlApplicationEngine::objectCreated,
        m_app,
        [moduleUrl](QObject *object, const QUrl &objectUrl) {
            if (!object && objectUrl == moduleUrl) {
                qCritical() << "Failed to create C-Opy main window";
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    m_engine->loadFromModule("org.ctoolbox.copy", "COpyMain");

    m_controller->openPaths(manifestPaths);
}

COpyApplication::~COpyApplication()
{
    delete m_engine;
    delete m_controller;
    delete m_instanceServer;
    delete m_app;
}

int COpyApplication::run()
{
    if (!m_app || m_forwarded) {
        return 1;
    }
    return m_app->exec();
}

bool COpyApplication::forwardedToRunningInstance() const
{
    return m_forwarded;
}

QString COpyApplication::version() const
{
    return kVersion;
}

bool COpyApplication::forwardToRunningInstance(const QStringList &manifestPaths)
{
    QLocalSocket socket;
    socket.connectToServer(kInstanceServerName);
    if (!socket.waitForConnected(500)) {
        return false;
    }

    QJsonObject message;
    message[QStringLiteral("type")] = QStringLiteral("open");
    message[QStringLiteral("paths")] = QJsonArray::fromStringList(manifestPaths);
    socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    qInfo() << "Forwarded" << manifestPaths.size() << "manifest(s) to the running C-Opy instance";
    return true;
}

void COpyApplication::startInstanceServer()
{
    m_instanceServer = new QLocalServer();
    QLocalServer::removeServer(kInstanceServerName);
    if (!m_instanceServer->listen(kInstanceServerName)) {
        qWarning() << "C-Opy could not listen for other instances:" << m_instanceServer->errorString();
        return;
    }

    connect(m_instanceServer, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *socket = m_instanceServer->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                while (socket->canReadLine()) {
                    handleInstanceMessage(socket->readLine().trimmed());
                }
            });
        }
    });
}

void COpyApplication::handleInstanceMessage(const QByteArray &line)
{
    const QJsonObject message = QJsonDocument::fromJson(line).object();
    if (message.value(QStringLiteral("type")).toString() != QStringLiteral("open")) {
        return;
    }
    const QJsonArray paths = message.value(QStringLiteral("paths")).toArray();
    QStringList selection;
    for (const QJsonValue &path : paths) {
        selection << path.toString();
    }
    m_controller->openPaths(selection);
    Q_EMIT activationRequested();
}
