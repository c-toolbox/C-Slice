/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CJOBSERVER_H
#define CJOBSERVER_H

#include <QObject>
#include <QLocalServer>
#include <QStringList>
#include <QMap>
#include <QJsonDocument>
#include <QVariant>
#include <QVector>

class CJobSocket;

class CJobServer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QStringList instanceIds READ instanceIds NOTIFY instancesChanged)
    Q_PROPERTY(QStringList jobNames READ jobNames NOTIFY jobsChanged)
    Q_PROPERTY(QStringList jobOwners READ jobOwners NOTIFY jobsChanged)
    Q_PROPERTY(bool isListening READ listening NOTIFY serverStarted)
    Q_PROPERTY(QString serverName READ serverName CONSTANT)
    Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY processingChanged)
    Q_PROPERTY(QString currentJobId READ currentJobId NOTIFY processingChanged)
    Q_PROPERTY(bool queueRunning READ isQueueRunning WRITE setQueueRunning NOTIFY queueRunningChanged)

public:
    explicit CJobServer(QObject *parent = nullptr);

    // Instance guard - check if another C-Job instance is already running
    static bool isAnotherInstanceRunning(const QString &serverName);
    ~CJobServer() override;

    bool startListening(const QString &serverName = QStringLiteral("C-Job"));
    void stopListening();

    QString serverName() const { return m_server ? m_server->fullServerName() : QString(); }
    bool listening() const { return m_server && m_server->isListening(); }

    // Job registry methods
    void addJob(const QString &instanceId, const QString &jobId);
    void removeJob(const QString &jobId);
    void updateJobStatus(const QString &jobId, const QString &status);

    // Instance management
    void registerInstance(CJobSocket *socket, const QString &instanceId);
    void unregisterInstance(const QString &instanceId);

    // Job queue management - single job at a time, sequential processing
    bool isProcessing() const { return m_processing; }
    QString currentJobId() const { return m_currentJobId; }
    bool isQueueRunning() const { return m_queueRunning; }
    void setQueueRunning(bool running);

    // Queue operations
    void addJobToQueue(const QString &instanceId, const QString &jobId);
    Q_INVOKABLE void addJobToQueueWithName(const QString &instanceId, const QString &jobId, const QString &jobName);
    void removeJobFromQueue(const QString &jobId);
    Q_INVOKABLE void removeJobFromQueueAt(int index);  // Remove job at specified index from queue
    Q_INVOKABLE void reorderJobQueue(int from, int to);  // Reorder items in the job queue
    void startNextJob();  // Start processing the next job in queue
    void completeCurrentJob();  // Mark current job as complete and move to next

    // Job assignment - send launch command to an instance
    void assignJobToInstance(const QString &jobId, const QString &instanceId);

    // Legacy method (deprecated)
    void assignJobToNextAvailable(const QString &jobId);

    // QML-accessible properties
    QString logText() const { return m_logText; }
    int jobCount() const { return m_jobs.keys().size(); }

    QStringList jobIds() const { return m_jobs.keys(); }
    QStringList jobNames() const;
    QStringList jobOwners() const;
    QStringList instanceIds() const { return m_instances.keys(); }

    // Q_INVOKABLE methods for QML
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void appendLog(const QString& message);
    Q_INVOKABLE bool isListening() const;
    Q_INVOKABLE QString getJobStatus(const QString &jobId) const;
    Q_INVOKABLE QString getServerName() const;

Q_SIGNALS:
    void serverStarted();
    void serverStopped();
    void jobAdded(const QString &instanceId, const QString &jobId);
    void jobRemoved(const QString &jobId);
    void jobProgressUpdated(const QString &jobId, int completed, int total);
    void logTextChanged();
    void instancesChanged();
    void jobsChanged();
    void processingChanged();  // Emitted when isProcessing or currentJobId changes
    void queueRunningChanged();  // Emitted when queue running state changes
    void jobLaunched(const QString &jobId, const QString &instanceId);
    void queueReordered();  // Emitted when queue is reordered

private Q_SLOTS:
    void onNewConnection();

private:
    struct JobInfo {
        QString instanceId;   // Owner/instance that submitted this job
        QString jobName;      // Descriptive name for display in UI
        QString status;
        int completed = 0;
        int total = 0;
    };

    void sendLaunchCommand(CJobSocket *socket, const QString &jobId);

    QLocalServer *m_server = nullptr;
    QList<CJobSocket *> m_sockets;
    QMap<QString, CJobSocket *> m_instances;  // instance_id -> socket

    // Job queue - sequential processing
    QVector<QString> m_jobQueue;  // Queue of job IDs waiting to be processed
    QString m_currentJobId;  // Currently processing job ID
    bool m_processing = false;  // Whether a job is currently being processed
    bool m_queueRunning = false;  // Whether queue auto-starts next job

    QMap<QString, JobInfo> m_jobs;  // job_id -> info

    mutable QString m_logText;
    QVector<QString> m_logLines;
};

#endif // CJOBSERVER_H