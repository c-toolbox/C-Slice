/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cjobserver.h"
#include "cjobsocket.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(cjobServer, "cjob.server")

// Unique server name for instance detection
static constexpr const char *CJOB_SERVER_NAME = "C-Job";

CJobServer::CJobServer(QObject *parent)
    : QObject(parent)
{
}

CJobServer::~CJobServer()
{
    stopListening();
}

bool CJobServer::isAnotherInstanceRunning(const QString &serverName)
{
    // Try to connect to the server - if successful, another instance is running
    QLocalSocket socket;
    socket.connectToServer(serverName);
    const bool isConnected = socket.waitForConnected(1000);
    socket.close();
    return isConnected;
}

bool CJobServer::startListening(const QString &serverName)
{
    if (m_server) {
        return true;
    }

    // Check if another instance is already running
    if (isAnotherInstanceRunning(serverName)) {
        qCWarning(cjobServer) << "Another C-Job instance is already running on server:" << serverName;
        return false;
    }

    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this, &CJobServer::onNewConnection);

    if (!m_server->listen(serverName)) {
        qCCritical(cjobServer) << "Failed to start C-Job server:" << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }

    qCInfo(cjobServer) << "C-Job server started on" << serverName;
    appendLog(QStringLiteral("C-Job server started on %1").arg(serverName));
    Q_EMIT serverStarted();
    return true;
}

void CJobServer::stopListening()
{
    if (m_server) {
        for (CJobSocket *socket : std::as_const(m_sockets)) {
            socket->deleteLater();
        }
        m_sockets.clear();
        m_instances.clear();
        m_jobs.clear();
        m_jobQueue.clear();
        m_currentJobId.clear();
        m_processing = false;

        QString serverName = m_server->fullServerName();
        delete m_server;
        m_server = nullptr;

        qCInfo(cjobServer) << "C-Job server stopped:" << serverName;
        Q_EMIT serverStopped();
    }
}

void CJobServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        CJobSocket *cjobSocket = new CJobSocket(socket, this);
        appendLog(QStringLiteral("New instance connected"));
        qCInfo(cjobServer) << "New client connected";
        connect(cjobSocket, &CJobSocket::socketClosed, this, [this](CJobSocket *s) {
            m_sockets.removeOne(s);
            QString instanceId = s->instanceId();
            if (!instanceId.isEmpty()) {
                m_instances.remove(instanceId);
            }
            delete s;
        });
        m_sockets.append(cjobSocket);
    }
}

void CJobServer::addJob(const QString &instanceId, const QString &jobId)
{
    JobInfo info;
    info.instanceId = instanceId;
    info.jobName = QStringLiteral("Job %1").arg(jobId.right(4));  // Default name if not provided
    info.status = QStringLiteral("Pending");
    m_jobs[jobId] = info;

    qCInfo(cjobServer) << "Job added:" << jobId << "from" << instanceId;
    Q_EMIT jobAdded(instanceId, jobId);
    Q_EMIT jobsChanged();  // Trigger QML property update
}

void CJobServer::removeJob(const QString &jobId)
{
    if (m_jobs.remove(jobId)) {
        qCInfo(cjobServer) << "Job removed:" << jobId;
        Q_EMIT jobRemoved(jobId);
        Q_EMIT jobsChanged();  // Trigger QML property update
    }
}

void CJobServer::updateJobStatus(const QString &jobId, const QString &status)
{
    if (m_jobs.contains(jobId)) {
        m_jobs[jobId].status = status;
        qCInfo(cjobServer) << "Job" << jobId << "status updated:" << status;
    }
}

void CJobServer::registerInstance(CJobSocket *socket, const QString &instanceId)
{
    if (!m_instances.contains(instanceId)) {
        m_instances[instanceId] = socket;
        qCInfo(cjobServer) << "Instance registered:" << instanceId;
        Q_EMIT instancesChanged();
    }
}

void CJobServer::unregisterInstance(const QString &instanceId)
{
    if (m_instances.contains(instanceId)) {
        m_instances.remove(instanceId);
        qCInfo(cjobServer) << "Instance unregistered:" << instanceId;
        Q_EMIT instancesChanged();
    }
}

void CJobServer::addJobToQueue(const QString &instanceId, const QString &jobId)
{
    addJobToQueueWithName(instanceId, jobId, QString());  // Empty name = auto-generated
}

void CJobServer::addJobToQueueWithName(const QString &instanceId, const QString &jobId, const QString &jobName)
{
    // Add job to queue if not already present and not currently processing
    if (m_jobQueue.contains(jobId)) {
        qCWarning(cjobServer) << "Job already in queue:" << jobId;
        return;
    }

    m_jobQueue.append(jobId);
    
    // Update or create job info with description
    JobInfo info;
    info.instanceId = instanceId;
    info.jobName = jobName.isEmpty() ? QStringLiteral("Job %1").arg(jobId.right(4)) : jobName;
    info.status = QStringLiteral("Queued");
    m_jobs[jobId] = info;

    qCInfo(cjobServer) << "Job added to queue:" << jobId << "(" << info.jobName << ") queue size:" << m_jobQueue.size();
    appendLog(QStringLiteral("Job %1 queued (position %2)").arg(jobId).arg(m_jobQueue.size()));
    
    Q_EMIT jobAdded(instanceId, jobId);
    Q_EMIT jobsChanged();

    // Only auto-start if queue is running and no job is currently processing
    if (m_queueRunning && !m_processing) {
        startNextJob();
    }
}

QStringList CJobServer::jobNames() const
{
    QStringList names;
    // Return names in queue order, not map key order
    for (const QString &jobId : m_jobQueue) {
        if (m_jobs.contains(jobId)) {
            names << m_jobs[jobId].jobName;
        } else {
            names << jobId;  // Fallback to ID if no job info
        }
    }
    return names;
}

QStringList CJobServer::jobOwners() const
{
    QStringList owners;
    for (const QString &jobId : m_jobQueue) {
        if (m_jobs.contains(jobId)) {
            owners << m_jobs[jobId].instanceId;
        } else {
            owners << QString();
        }
    }
    return owners;
}

void CJobServer::removeJobFromQueue(const QString &jobId)
{
    // Can only remove from queue if it's not the current job
    if (m_currentJobId == jobId) {
        qCWarning(cjobServer) << "Cannot remove currently processing job:" << jobId;
        return;
    }

    if (m_jobQueue.removeAll(jobId) > 0) {
        qCInfo(cjobServer) << "Job removed from queue:" << jobId;
        appendLog(QStringLiteral("Job %1 removed from queue").arg(jobId));
        
        // Update job status
        if (m_jobs.contains(jobId)) {
            m_jobs[jobId].status = QStringLiteral("Cancelled");
        }
        
        Q_EMIT jobsChanged();
    }
}

void CJobServer::removeJobFromQueueAt(int index)
{
    if (index < 0 || index >= m_jobQueue.size()) {
        return;
    }

    // Can only remove from queue if it's not the current job being processed
    if (m_processing && !m_jobQueue.isEmpty() && m_currentJobId == m_jobQueue.first()) {
        qCWarning(cjobServer) << "Cannot remove job while processing";
        return;
    }

    const QString jobId = m_jobQueue.takeAt(index);
    qCInfo(cjobServer) << "Removed job from queue at index" << index << ":" << jobId;
    
    // Update job status to cancelled
    if (m_jobs.contains(jobId)) {
        m_jobs[jobId].status = QStringLiteral("Cancelled");
    }
    
    Q_EMIT jobsChanged();
}

void CJobServer::reorderJobQueue(int from, int to)
{
    if (from < 0 || from >= m_jobQueue.size() || 
        to < 0 || to >= m_jobQueue.size() || 
        from == to) {
        return;
    }

    // Move the job ID from 'from' position to 'to' position in the queue
    const QString jobId = m_jobQueue.takeAt(from);
    const int insertPos = qBound(0, to, m_jobQueue.size());
    m_jobQueue.insert(insertPos, jobId);

    qCDebug(cjobServer) << "Reordered job queue: moved item" << from << "to" << to;
    
    // Emit both signals to ensure QML updates properly
    Q_EMIT jobsChanged();
    Q_EMIT queueReordered();
}

void CJobServer::startNextJob()
{
    if (m_processing || m_jobQueue.isEmpty()) {
        return;
    }

    // Get next job from queue
    QString jobId = m_jobQueue.takeFirst();
    
    // Find which instance submitted this job
    QString submittingInstanceId;
    if (m_jobs.contains(jobId)) {
        submittingInstanceId = m_jobs[jobId].instanceId;
    }

    // For now, assign to the submitting instance
    // In future, could implement load balancing across instances
    CJobSocket *targetSocket = nullptr;
    if (!submittingInstanceId.isEmpty() && m_instances.contains(submittingInstanceId)) {
        targetSocket = m_instances[submittingInstanceId];
    } else {
        // Fall back to any available instance
        for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
            targetSocket = it.value();
            break;
        }
    }

    if (!targetSocket) {
        qCWarning(cjobServer) << "No available instances for job:" << jobId;
        appendLog(QStringLiteral("Job %1 failed: no available instances").arg(jobId));
        
        // Mark job as failed and try next
        if (m_jobs.contains(jobId)) {
            m_jobs[jobId].status = QStringLiteral("Failed");
        }
        startNextJob();  // Try next job in queue
        return;
    }

    // Set this job as currently processing
    m_processing = true;
    m_currentJobId = jobId;
    
    if (m_jobs.contains(jobId)) {
        m_jobs[jobId].status = QStringLiteral("Running");
    }

    qCInfo(cjobServer) << "Starting job:" << jobId << "on instance:" << submittingInstanceId;
    appendLog(QStringLiteral("Job %1 started on instance %2").arg(jobId).arg(submittingInstanceId));

    // Send launch command to the target instance
    sendLaunchCommand(targetSocket, jobId);
    
    Q_EMIT processingChanged();
    Q_EMIT jobLaunched(jobId, submittingInstanceId);
}

void CJobServer::completeCurrentJob()
{
    if (!m_processing) {
        return;
    }

    qCInfo(cjobServer) << "Current job complete:" << m_currentJobId;
    appendLog(QStringLiteral("Job %1 completed").arg(m_currentJobId));

    // Update job status
    if (m_jobs.contains(m_currentJobId)) {
        m_jobs[m_currentJobId].status = QStringLiteral("Complete");
    }

    // Clear current job state
    QString completedJobId = m_currentJobId;
    m_currentJobId.clear();
    m_processing = false;

    Q_EMIT jobsChanged();
    Q_EMIT processingChanged();

    // Try to start next job in queue only if queue is running
    if (m_queueRunning) {
        startNextJob();
    }
}

void CJobServer::setQueueRunning(bool running)
{
    if (m_queueRunning == running) {
        return;
    }

    m_queueRunning = running;
    
    qCInfo(cjobServer) << "Queue running state changed to:" << running;
    appendLog(QStringLiteral("Queue %1").arg(running ? QStringLiteral("started") : QStringLiteral("paused")));

    Q_EMIT queueRunningChanged();

    // If starting the queue and no job is processing, try to start next
    if (running && !m_processing) {
        startNextJob();
    }
}

void CJobServer::assignJobToInstance(const QString &jobId, const QString &instanceId)
{
    if (!m_instances.contains(instanceId)) {
        qCWarning(cjobServer) << "Unknown instance:" << instanceId;
        return;
    }

    CJobSocket *targetSocket = m_instances[instanceId];
    
    // Update job info with assigned instance
    JobInfo info;
    info.instanceId = instanceId;
    info.status = QStringLiteral("Assigned");
    m_jobs[jobId] = info;

    qCInfo(cjobServer) << "Job" << jobId << "assigned to instance:" << instanceId;
    
    // Send launch command
    sendLaunchCommand(targetSocket, jobId);
    
    Q_EMIT jobLaunched(jobId, instanceId);
}

void CJobServer::sendLaunchCommand(CJobSocket *socket, const QString &jobId)
{
    if (!socket || !socket->socket()->isOpen()) {
        qCWarning(cjobServer) << "Cannot send launch command: socket not open";
        return;
    }

    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("launch_job");
    json[QStringLiteral("job_id")] = jobId;

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append('\n');

    socket->socket()->write(data);
    socket->socket()->flush();

    qCInfo(cjobServer) << "Sent launch command for job:" << jobId;
}

void CJobServer::assignJobToNextAvailable(const QString &jobId)
{
    // Find first available instance
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        qCInfo(cjobServer) << "Assigning job" << jobId << "to instance" << it.key();
        assignJobToInstance(jobId, it.key());
        break;
    }
}

QString CJobServer::getJobStatus(const QString &jobId) const
{
    if (m_jobs.contains(jobId)) {
        return m_jobs[jobId].status;
    }
    return QStringLiteral("Unknown");
}

bool CJobServer::isListening() const
{
    return listening();
}

QString CJobServer::getServerName() const
{
    return serverName();
}

void CJobServer::clearLog()
{
    m_logLines.clear();
    m_logText.clear();
    Q_EMIT logTextChanged();
}

void CJobServer::appendLog(const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    
    m_logLines.append(trimmed);
    m_logText = m_logLines.join(QLatin1Char('\n'));
    Q_EMIT logTextChanged();
}