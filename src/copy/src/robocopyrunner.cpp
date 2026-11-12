/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "robocopyrunner.h"
#include "copycredentials.h"

#include <QProcess>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>

#ifdef Q_OS_WIN
#include <QWinEventNotifier>

#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
// Quotes one argument for the MSVC runtime command-line parser used by robocopy.
QString quoteArgument(const QString &argument)
{
    static const QRegularExpression needsQuotes(QStringLiteral("[\\s\"]"));
    if (!argument.isEmpty() && !needsQuotes.match(argument).hasMatch()) {
        return argument;
    }
    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar c : argument) {
        if (c == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (c == QLatin1Char('"')) {
            quoted += QString(backslashes * 2 + 1, QLatin1Char('\\')) + QLatin1Char('"');
        } else {
            quoted += QString(backslashes, QLatin1Char('\\')) + c;
        }
        backslashes = 0;
    }
    quoted += QString(backslashes * 2, QLatin1Char('\\')) + QLatin1Char('"');
    return quoted;
}

QString robocopyExecutable()
{
    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    return QString::fromWCharArray(systemDirectory, static_cast<int>(length)) + QStringLiteral("\\robocopy.exe");
}
#endif

} // namespace

RobocopyRunner::RobocopyRunner(QObject *parent)
    : QObject(parent)
{
}

RobocopyRunner::~RobocopyRunner()
{
    m_finished = true;
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
#ifdef Q_OS_WIN
    if (m_processHandle) {
        // Ending the process closes its end of the pipe, which ends the reader thread.
        TerminateProcess(m_processHandle, 1);
        WaitForSingleObject(m_processHandle, 2000);
    }
    if (m_readerThread) {
        m_readerThread->wait(2000);
        delete m_readerThread;
    }
    delete m_exitNotifier;
    if (m_processHandle) {
        CloseHandle(m_processHandle);
    }
    if (m_queryHandle) {
        CloseHandle(m_queryHandle);
    }
#endif
}

void RobocopyRunner::start(const QStringList &arguments, const QString &accountName)
{
    if (!accountName.isEmpty()) {
        startAsAccount(arguments, accountName);
        return;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QByteArray bytes = m_process->readAllStandardOutput();
        m_outputBytes += bytes.size();
        Q_EMIT output(QString::fromLocal8Bit(bytes));
    });
#ifdef Q_OS_WIN
    connect(m_process, &QProcess::started, this, [this]() {
        m_queryHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(m_process->processId()));
    });
#endif
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        finishOnce(exitCode, exitStatus == QProcess::CrashExit ? QStringLiteral("robocopy was terminated.") : QString());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finishOnce(-1, QStringLiteral("Could not start robocopy: %1").arg(m_process->errorString()));
        }
    });
    m_process->start(QStringLiteral("robocopy"), arguments);
}

void RobocopyRunner::kill()
{
    m_killed = true;
    if (m_process) {
        m_process->kill();
    }
#ifdef Q_OS_WIN
    if (m_processHandle) {
        TerminateProcess(m_processHandle, 1);
    }
#endif
}

qint64 RobocopyRunner::writtenBytes() const
{
#ifdef Q_OS_WIN
    const HANDLE handle = m_processHandle ? m_processHandle : m_queryHandle;
    IO_COUNTERS counters {};
    if (handle && GetProcessIoCounters(handle, &counters)) {
        return std::max<qint64>(0, static_cast<qint64>(counters.WriteTransferCount) - m_outputBytes);
    }
#endif
    return -1;
}

void RobocopyRunner::startAsAccount(const QStringList &arguments, const QString &accountName)
{
#ifdef Q_OS_WIN
    QString userName;
    std::wstring password;
    QString error;
    if (!COpyCredentials::read(accountName, &userName, &password, &error)) {
        failLater(QStringLiteral("Could not use account '%1': %2").arg(accountName, error));
        return;
    }

    // "DOMAIN\user" or "MACHINE\user"; a UPN ("user@domain") is passed without a domain.
    QString domain;
    QString user = userName;
    const int separator = userName.indexOf(QLatin1Char('\\'));
    if (separator >= 0) {
        domain = userName.left(separator);
        user = userName.mid(separator + 1);
    }
    const std::wstring wideUser = user.toStdWString();
    const std::wstring wideDomain = domain.toStdWString();

    SECURITY_ATTRIBUTES security { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
        failLater(QStringLiteral("Could not create output pipe: %1").arg(qt_error_string(static_cast<int>(GetLastError()))));
        return;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;

    const QString program = robocopyExecutable();
    QStringList commandParts { quoteArgument(program) };
    for (const QString &argument : arguments) {
        commandParts << quoteArgument(argument);
    }
    std::wstring commandLine = commandParts.join(QLatin1Char(' ')).toStdWString();
    const std::wstring applicationName = program.toStdWString();

    PROCESS_INFORMATION processInfo {};
    // LOGON_NETCREDENTIALS_ONLY: robocopy keeps the local identity of the current user and
    // presents the stored account only when it connects to network shares.
    const BOOL ok = CreateProcessWithLogonW(wideUser.c_str(),
        domain.isEmpty() ? nullptr : wideDomain.c_str(),
        password.c_str(),
        LOGON_NETCREDENTIALS_ONLY,
        applicationName.c_str(),
        commandLine.data(),
        CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        nullptr,
        &startup,
        &processInfo);
    const DWORD lastError = GetLastError();
    SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
    CloseHandle(writePipe);

    if (!ok) {
        CloseHandle(readPipe);
        failLater(QStringLiteral("Could not start robocopy as '%1': %2").arg(userName, qt_error_string(static_cast<int>(lastError))));
        return;
    }
    CloseHandle(processInfo.hThread);
    m_processHandle = processInfo.hProcess;

    m_readerThread = QThread::create([this, readPipe]() {
        char buffer[4096];
        DWORD bytesRead = 0;
        while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
            const QString text = QString::fromLocal8Bit(buffer, static_cast<qsizetype>(bytesRead));
            QMetaObject::invokeMethod(this, [this, text, bytesRead]() {
                m_outputBytes += bytesRead;
                Q_EMIT output(text);
            }, Qt::QueuedConnection);
        }
        CloseHandle(readPipe);
    });
    connect(m_readerThread, &QThread::finished, this, [this]() {
        m_readerDone = true;
        maybeFinishAccountProcess();
    });
    m_readerThread->start();

    m_exitNotifier = new QWinEventNotifier(m_processHandle, this);
    connect(m_exitNotifier, &QWinEventNotifier::activated, this, [this]() {
        m_exitNotifier->setEnabled(false);
        DWORD exitCode = 1;
        GetExitCodeProcess(m_processHandle, &exitCode);
        m_exitCode = static_cast<int>(exitCode);
        m_processExited = true;
        maybeFinishAccountProcess();
    });
#else
    Q_UNUSED(arguments)
    failLater(QStringLiteral("Account '%1' cannot be used: stored accounts require Windows.").arg(accountName));
#endif
}

void RobocopyRunner::maybeFinishAccountProcess()
{
#ifdef Q_OS_WIN
    // Wait for both the exit and the last output, so the summary is never cut off.
    if (m_processExited && m_readerDone) {
        finishOnce(m_exitCode, m_killed ? QStringLiteral("robocopy was terminated.") : QString());
    }
#endif
}

void RobocopyRunner::failLater(const QString &failure)
{
    // Queued so that callers never see finished() from inside start().
    QMetaObject::invokeMethod(this, [this, failure]() {
        finishOnce(-1, failure);
    }, Qt::QueuedConnection);
}

void RobocopyRunner::finishOnce(int exitCode, const QString &failure)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    Q_EMIT finished(exitCode, failure);
}
