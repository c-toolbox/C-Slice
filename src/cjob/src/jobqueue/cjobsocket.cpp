/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cjobsocket.h"
#include "cjobserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(cjobSocket, "cjob.socket")

CJobSocket::CJobSocket(QLocalSocket *socket, CJobServer *server, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_server(server)
{
    connect(m_socket, &QLocalSocket::readyRead, this, &CJobSocket::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, &CJobSocket::onDisconnected);
}

CJobSocket::~CJobSocket()
{
    if (m_socket && m_socket->isOpen()) {
        m_socket->close();
    }
}

void CJobSocket::onReadyRead()
{
    while (m_socket && m_socket->bytesAvailable() > 0) {
        QByteArray data = m_socket->readAll();
        m_buffer += QString::fromUtf8(data);

        // Process complete messages
        int newlinePos;
        while ((newlinePos = m_buffer.indexOf(QStringLiteral("\n"))) >= 0) {
            QString message = m_buffer.left(newlinePos);
            m_buffer = m_buffer.mid(newlinePos + 1);

            if (!message.trimmed().isEmpty()) {
                processMessage(message);
            }
        }
    }
}

void CJobSocket::onDisconnected()
{
    qCInfo(cjobSocket) << "Socket disconnected";
    Q_EMIT socketClosed(this);
}

void CJobSocket::processMessage(const QString &message)
{
    qCDebug(cjobSocket) << "Received:" << message;

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        qCWarning(cjobSocket) << "Invalid JSON message";
        return;
    }

    QJsonObject obj = doc.object();
    QString type = obj[QStringLiteral("type")].toString();

    if (type == QLatin1String("register_instance")) {
        m_instanceId = obj[QStringLiteral("instance_id")].toString();
        if (!m_instanceId.isEmpty() && m_server) {
            m_server->registerInstance(this, m_instanceId);
            qCInfo(cjobSocket) << "Instance registered:" << m_instanceId;
        }
    } else if (type == QLatin1String("submit_job")) {
        QString jobId = obj[QStringLiteral("job_id")].toString();
        QString jobName = obj[QStringLiteral("job_name")].toString();
        if (!jobId.isEmpty() && m_server) {
            // Add to queue with description and let C-Job handle scheduling
            m_server->addJobToQueueWithName(m_instanceId, jobId, jobName);
        }
    } else if (type == QLatin1String("progress_update")) {
        QString jobId = obj[QStringLiteral("job_id")].toString();
        int completed = obj[QStringLiteral("completed")].toInt(0);
        int total = obj[QStringLiteral("total")].toInt(0);

        if (!jobId.isEmpty() && m_server) {
            m_server->updateJobStatus(jobId, QStringLiteral("Running"));
            Q_EMIT m_server->jobProgressUpdated(jobId, completed, total);
        }
    } else if (type == QLatin1String("complete_job")) {
        QString jobId = obj[QStringLiteral("job_id")].toString();
        qCInfo(cjobSocket) << "Job completed notification:" << jobId;
        
        // Tell server this job is complete so it can move to next in queue
        if (m_server) {
            m_server->completeCurrentJob();
        }
    } else {
        qCWarning(cjobSocket) << "Unknown message type:" << type;
    }
}

void CJobSocket::sendJson(const QJsonObject &json)
{
    if (!m_socket || !m_socket->isOpen()) {
        return;
    }

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append('\n');

    m_socket->write(data);
}