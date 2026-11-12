/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "copycredentials.h"

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace COpyCredentials {

namespace {

const QString kTargetPrefix = QStringLiteral("C-Opy/");

#ifdef Q_OS_WIN
std::wstring targetName(const QString &name)
{
    return QString(kTargetPrefix + name).toStdWString();
}
#endif

} // namespace

bool supported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QList<Account> accounts()
{
    QList<Account> result;
#ifdef Q_OS_WIN
    DWORD count = 0;
    PCREDENTIALW *credentials = nullptr;
    const std::wstring filter = QString(kTargetPrefix + QLatin1Char('*')).toStdWString();
    if (!CredEnumerateW(filter.c_str(), 0, &count, &credentials)) {
        return result;
    }
    for (DWORD i = 0; i < count; ++i) {
        if (credentials[i]->Type != CRED_TYPE_GENERIC) {
            continue;
        }
        Account account;
        account.name = QString::fromWCharArray(credentials[i]->TargetName).mid(kTargetPrefix.size());
        if (credentials[i]->UserName) {
            account.userName = QString::fromWCharArray(credentials[i]->UserName);
        }
        result << account;
    }
    CredFree(credentials);
    std::sort(result.begin(), result.end(), [](const Account &a, const Account &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
#endif
    return result;
}

bool store(const QString &name, const QString &userName, const QString &password, QString *error)
{
#ifdef Q_OS_WIN
    if (name.trimmed().isEmpty() || userName.trimmed().isEmpty()) {
        *error = QStringLiteral("An account needs a name and a user name.");
        return false;
    }
    std::wstring target = targetName(name.trimmed());
    std::wstring user = userName.trimmed().toStdWString();
    std::wstring secret = password.toStdWString();
    std::wstring comment = L"C-Opy network account for robocopy";

    CREDENTIALW credential {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.UserName = user.data();
    credential.Comment = comment.data();
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size() * sizeof(wchar_t));
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(secret.data());
    // Kept for this Windows user on this machine only; not roamed to other machines.
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;

    const BOOL ok = CredWriteW(&credential, 0);
    const DWORD lastError = GetLastError();
    SecureZeroMemory(secret.data(), secret.size() * sizeof(wchar_t));
    if (!ok) {
        *error = qt_error_string(static_cast<int>(lastError));
        return false;
    }
    return true;
#else
    Q_UNUSED(name)
    Q_UNUSED(userName)
    Q_UNUSED(password)
    *error = QStringLiteral("Stored accounts require Windows.");
    return false;
#endif
}

bool remove(const QString &name, QString *error)
{
#ifdef Q_OS_WIN
    if (!CredDeleteW(targetName(name).c_str(), CRED_TYPE_GENERIC, 0)) {
        *error = qt_error_string(static_cast<int>(GetLastError()));
        return false;
    }
    return true;
#else
    Q_UNUSED(name)
    *error = QStringLiteral("Stored accounts require Windows.");
    return false;
#endif
}

bool read(const QString &name, QString *userName, std::wstring *password, QString *error)
{
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(targetName(name).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        const DWORD lastError = GetLastError();
        *error = lastError == ERROR_NOT_FOUND
            ? QStringLiteral("account '%1' is not stored in Windows Credential Manager").arg(name)
            : qt_error_string(static_cast<int>(lastError));
        return false;
    }
    *userName = credential->UserName ? QString::fromWCharArray(credential->UserName) : QString();
    password->assign(reinterpret_cast<const wchar_t *>(credential->CredentialBlob),
        credential->CredentialBlobSize / sizeof(wchar_t));
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return true;
#else
    Q_UNUSED(name)
    Q_UNUSED(userName)
    Q_UNUSED(password)
    *error = QStringLiteral("Stored accounts require Windows.");
    return false;
#endif
}

} // namespace COpyCredentials
