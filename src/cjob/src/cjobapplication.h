/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CJOBAPPLICATION_H
#define CJOBAPPLICATION_H

#include <QObject>
#include <QString>
#include <QUrl>

class QGuiApplication;
class QQmlApplicationEngine;
class CJobServer;

class CJobApplication : public QObject {
    Q_OBJECT

public:
    explicit CJobApplication(int &argc, char **argv);
    ~CJobApplication() override;

    int run();

    CJobServer *server();

    QString version() const;

Q_SIGNALS:
    void serverChanged();

private:
    QGuiApplication *m_app = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;
    CJobServer *m_server = nullptr;
};

#endif // CJOBAPPLICATION_H