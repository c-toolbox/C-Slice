/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CJOB_SOCKET_H
#define CJOB_SOCKET_H

#include <QObject>
#include <QLocalSocket>
#include <QString>

class CJobServer;

class CJobSocket : public QObject {
    Q_OBJECT

public:
    explicit CJobSocket(QLocalSocket *socket, CJobServer *server, QObject *parent = nullptr);
    ~CJobSocket() override;

    QString instanceId() const { return m_instanceId; }
    QLocalSocket *socket() { return m_socket; }

Q_SIGNALS:
    void socketClosed(CJobSocket *socket);
    void jobCompleted(const QString &jobId);  // Job completed signal to server

private Q_SLOTS:
    void onReadyRead();
    void onDisconnected();

private:
    void processMessage(const QString &message);
    void sendJson(const QJsonObject &json);

    QLocalSocket *m_socket = nullptr;
    CJobServer *m_server = nullptr;
    QString m_instanceId;
    QString m_buffer;
};

#endif // CJOB_SOCKET_H
