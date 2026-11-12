/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef COPYAPPLICATION_H
#define COPYAPPLICATION_H

#include <QObject>
#include <QString>
#include <QStringList>

class QGuiApplication;
class QLocalServer;
class QQmlApplicationEngine;
class COpyController;

class COpyApplication : public QObject {
    Q_OBJECT

public:
    explicit COpyApplication(int &argc, char **argv);
    ~COpyApplication() override;

    int run();

    // True when another C-Opy instance was found and received this launch's manifests.
    bool forwardedToRunningInstance() const;

    Q_INVOKABLE QString version() const;

Q_SIGNALS:
    void activationRequested();

private:
    bool forwardToRunningInstance(const QStringList &manifestPaths);
    void startInstanceServer();
    void handleInstanceMessage(const QByteArray &line);

    QGuiApplication *m_app = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;
    QLocalServer *m_instanceServer = nullptr;
    COpyController *m_controller = nullptr;
    bool m_forwarded = false;
};

#endif // COPYAPPLICATION_H
