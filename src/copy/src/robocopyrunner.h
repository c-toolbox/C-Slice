/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ROBOCOPYRUNNER_H
#define ROBOCOPYRUNNER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QThread;
class QWinEventNotifier;

// Runs one robocopy process, either as the current user or with a stored account from
// COpyCredentials. A stored account is applied as network-only credentials (like
// "runas /netonly"): only this robocopy process authenticates with it.
class RobocopyRunner : public QObject {
    Q_OBJECT

public:
    explicit RobocopyRunner(QObject *parent = nullptr);
    ~RobocopyRunner() override;

    void start(const QStringList &arguments, const QString &accountName = QString());
    void kill();
    // Bytes robocopy has written to files so far, from its process I/O counters; -1 when
    // unknown. Unlike the listed files this follows the actual copy, also with /MT and /NP.
    qint64 writtenBytes() const;

Q_SIGNALS:
    void output(const QString &text);
    // Emitted once. failure is empty when robocopy ran and exitCode is its exit code.
    void finished(int exitCode, const QString &failure);

private:
    void startAsAccount(const QStringList &arguments, const QString &accountName);
    void failLater(const QString &failure);
    void finishOnce(int exitCode, const QString &failure);
    void maybeFinishAccountProcess();

    QProcess *m_process = nullptr;
    bool m_finished = false;
    bool m_killed = false;
    // Robocopy's own output also counts as written bytes.
    qint64 m_outputBytes = 0;

#ifdef Q_OS_WIN
    // Opened for the I/O counters of a robocopy started with QProcess.
    void *m_queryHandle = nullptr;
    void *m_processHandle = nullptr;
    QThread *m_readerThread = nullptr;
    QWinEventNotifier *m_exitNotifier = nullptr;
    bool m_processExited = false;
    bool m_readerDone = false;
    int m_exitCode = -1;
#endif
};

#endif // ROBOCOPYRUNNER_H
