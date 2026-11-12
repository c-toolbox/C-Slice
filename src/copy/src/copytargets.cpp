/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "copytargets.h"
#include "copycredentials.h"

#include <QDir>
#include <QFileInfo>

#include <string>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace COpyTargets {

namespace {

#ifdef Q_OS_WIN
// Network-only credentials of a stored account on the current thread, until destroyed.
class NetworkLogon {
public:
    NetworkLogon() = default;
    NetworkLogon(const NetworkLogon &) = delete;
    NetworkLogon &operator=(const NetworkLogon &) = delete;

    ~NetworkLogon()
    {
        if (m_impersonating) {
            RevertToSelf();
        }
        if (m_token) {
            CloseHandle(m_token);
        }
    }

    // Without an account the thread keeps the current Windows user.
    bool begin(const QString &accountName, QString *error)
    {
        if (accountName.isEmpty()) {
            return true;
        }
        QString userName;
        std::wstring password;
        QString readError;
        if (!COpyCredentials::read(accountName, &userName, &password, &readError)) {
            *error = QStringLiteral("could not use account '%1': %2").arg(accountName, readError);
            return false;
        }
        // "DOMAIN\user" or "MACHINE\user"; a UPN ("user@domain") is passed without a domain.
        QString domain;
        QString user = userName;
        const qsizetype separator = userName.indexOf(QLatin1Char('\\'));
        if (separator >= 0) {
            domain = userName.left(separator);
            user = userName.mid(separator + 1);
        }
        const std::wstring wideUser = user.toStdWString();
        const std::wstring wideDomain = domain.toStdWString();
        // LOGON32_LOGON_NEW_CREDENTIALS: the local identity stays the current user; the
        // account is presented only to network shares, like CreateProcessWithLogonW's
        // LOGON_NETCREDENTIALS_ONLY for robocopy.
        const BOOL ok = LogonUserW(wideUser.c_str(), domain.isEmpty() ? nullptr : wideDomain.c_str(), password.c_str(),
            LOGON32_LOGON_NEW_CREDENTIALS, LOGON32_PROVIDER_WINNT50, &m_token);
        const DWORD lastError = GetLastError();
        SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
        if (!ok) {
            m_token = nullptr;
            *error = QStringLiteral("could not log on as '%1': %2").arg(userName, qt_error_string(static_cast<int>(lastError)));
            return false;
        }
        if (!ImpersonateLoggedOnUser(m_token)) {
            *error = QStringLiteral("could not use account '%1': %2").arg(accountName, qt_error_string(static_cast<int>(GetLastError())));
            return false;
        }
        m_impersonating = true;
        return true;
    }

private:
    HANDLE m_token = nullptr;
    bool m_impersonating = false;
};

// \\?\D:\... or \\?\UNC\host\share\..., so long paths work as well.
std::wstring nativePath(const QString &path)
{
    const QString native = QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    if (native.startsWith(QStringLiteral("\\\\?\\"))) {
        return native.toStdWString();
    }
    if (native.startsWith(QStringLiteral("\\\\"))) {
        return QString(QStringLiteral("\\\\?\\UNC\\") + native.mid(2)).toStdWString();
    }
    return QString(QStringLiteral("\\\\?\\") + native).toStdWString();
}

// \\?\UNC\host\share of a UNC path; empty for a local path.
std::wstring shareRoot(const std::wstring &path)
{
    static const std::wstring prefix = L"\\\\?\\UNC\\";
    if (path.rfind(prefix, 0) != 0) {
        return std::wstring();
    }
    const size_t host = path.find(L'\\', prefix.size());
    if (host == std::wstring::npos) {
        return std::wstring();
    }
    const size_t share = path.find(L'\\', host + 1);
    return share == std::wstring::npos ? path : path.substr(0, share);
}

QString errorText(DWORD error)
{
    return qt_error_string(static_cast<int>(error));
}

bool isNotFound(DWORD error)
{
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

bool isLink(DWORD attributes)
{
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool isFolder(DWORD attributes)
{
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

struct Entry {
    std::wstring path;
    DWORD attributes = 0;
    qint64 size = 0;
};

// The entries of a folder that match a pattern, without "." and "..". Collected first, so
// they can be deleted without changing the folder while it is listed. Nothing matching is
// not an error.
DWORD listEntries(const std::wstring &folder, const std::wstring &pattern, std::vector<Entry> *entries)
{
    WIN32_FIND_DATAW data;
    const std::wstring query = folder + L"\\" + pattern;
    const HANDLE find = FindFirstFileExW(query.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES ? NO_ERROR : error;
    }
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        entries->push_back({ folder + L"\\" + name, data.dwFileAttributes,
            static_cast<qint64>((static_cast<quint64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow) });
    } while (FindNextFileW(find, &data));
    const DWORD error = GetLastError();
    FindClose(find);
    return error == ERROR_NO_MORE_FILES ? NO_ERROR : error;
}

// The path as the user wrote it, without the \\?\ prefix.
QString displayPath(const std::wstring &path)
{
    const QString text = QString::fromStdWString(path);
    if (text.startsWith(QStringLiteral("\\\\?\\UNC\\"))) {
        return QStringLiteral("\\\\") + text.mid(8);
    }
    return text.startsWith(QStringLiteral("\\\\?\\")) ? text.mid(4) : text;
}

void keepFirstError(QString *error, DWORD code, const std::wstring &path)
{
    if (error->isEmpty()) {
        *error = QStringLiteral("%1: %2").arg(displayPath(path), errorText(code));
    }
}

// Every file and folder below a folder; links are counted, not followed.
void countTree(const std::wstring &folder, Inspection &result, const std::atomic_bool &cancel)
{
    std::vector<Entry> entries;
    if (const DWORD error = listEntries(folder, L"*", &entries); error != NO_ERROR) {
        keepFirstError(&result.error, error, folder);
    }
    for (const Entry &entry : entries) {
        if (cancel) {
            return;
        }
        if (isFolder(entry.attributes)) {
            ++result.folderCount;
            if (!isLink(entry.attributes)) {
                countTree(entry.path, result, cancel);
            }
        } else {
            ++result.fileCount;
            result.totalBytes += entry.size;
        }
    }
}

// Files (not folders) that match the pattern, in subfolders too when recursive.
template<typename Visit>
void forEachMatch(const std::wstring &folder, const std::wstring &pattern, bool recursive, QString *error,
    const std::atomic_bool &cancel, Visit visit)
{
    std::vector<Entry> matches;
    if (const DWORD code = listEntries(folder, pattern, &matches); code != NO_ERROR) {
        keepFirstError(error, code, folder);
    }
    for (const Entry &entry : matches) {
        if (cancel) {
            return;
        }
        if (!isFolder(entry.attributes)) {
            visit(entry);
        }
    }
    if (!recursive) {
        return;
    }
    std::vector<Entry> folders;
    if (const DWORD code = listEntries(folder, L"*", &folders); code != NO_ERROR) {
        keepFirstError(error, code, folder);
    }
    for (const Entry &entry : folders) {
        if (cancel) {
            return;
        }
        // Robocopy excludes junctions (/XJ), so their files are never copied there.
        if (isFolder(entry.attributes) && !isLink(entry.attributes)) {
            forEachMatch(entry.path, pattern, true, error, cancel, visit);
        }
    }
}

struct Removal {
    int failed = 0;
    QString error;

    void fail(DWORD code, const std::wstring &path)
    {
        ++failed;
        keepFirstError(&error, code, path);
    }
};

void removeEntry(const Entry &entry, Removal &removal, const std::atomic_bool &cancel)
{
    if (entry.attributes & FILE_ATTRIBUTE_READONLY) {
        SetFileAttributesW(entry.path.c_str(), entry.attributes & ~FILE_ATTRIBUTE_READONLY);
    }
    if (!isFolder(entry.attributes)) {
        if (!DeleteFileW(entry.path.c_str())) {
            removal.fail(GetLastError(), entry.path);
        }
        return;
    }
    // A junction or a folder symbolic link is removed itself; what it points to stays.
    if (!isLink(entry.attributes)) {
        std::vector<Entry> children;
        if (const DWORD error = listEntries(entry.path, L"*", &children); error != NO_ERROR) {
            removal.fail(error, entry.path);
            return;
        }
        for (const Entry &child : children) {
            if (cancel) {
                return;
            }
            removeEntry(child, removal, cancel);
        }
    }
    if (!RemoveDirectoryW(entry.path.c_str())) {
        removal.fail(GetLastError(), entry.path);
    }
}
#endif

} // namespace

Inspection inspect(const Target &target, const std::atomic_bool &cancel)
{
    Inspection result;
#ifdef Q_OS_WIN
    NetworkLogon logon;
    if (!logon.begin(target.accountName, &result.error)) {
        return result;
    }
    const std::wstring folder = nativePath(target.folder);
    WIN32_FILE_ATTRIBUTE_DATA data {};
    if (!GetFileAttributesExW(folder.c_str(), GetFileExInfoStandard, &data)) {
        const DWORD error = GetLastError();
        if (!isNotFound(error)) {
            result.error = errorText(error);
            return result;
        }
        // Not there: the node is reachable when its share is.
        const std::wstring share = shareRoot(folder);
        if (!share.empty() && GetFileAttributesW((share + L"\\").c_str()) == INVALID_FILE_ATTRIBUTES) {
            result.error = errorText(GetLastError());
            return result;
        }
        result.reachable = true;
        return result;
    }
    result.reachable = true;
    const DWORD attributes = data.dwFileAttributes;
    if (target.pattern == QStringLiteral("*")) {
        result.exists = true;
        if (!isFolder(attributes)) {
            result.fileCount = 1;
            result.totalBytes = static_cast<qint64>((static_cast<quint64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
        } else if (!isLink(attributes)) {
            countTree(folder, result, cancel);
        }
        return result;
    }
    if (isFolder(attributes)) {
        forEachMatch(folder, target.pattern.toStdWString(), target.recursive, &result.error, cancel, [&result](const Entry &entry) {
            ++result.fileCount;
            result.totalBytes += entry.size;
        });
    }
    result.exists = result.fileCount > 0;
#else
    Q_UNUSED(target)
    Q_UNUSED(cancel)
    result.error = QStringLiteral("checking destinations requires Windows");
#endif
    return result;
}

bool remove(const Target &target, const std::atomic_bool &cancel, QString *error)
{
#ifdef Q_OS_WIN
    NetworkLogon logon;
    if (!logon.begin(target.accountName, error)) {
        return false;
    }
    const std::wstring folder = nativePath(target.folder);
    const DWORD attributes = GetFileAttributesW(folder.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        if (isNotFound(code)) {
            return true;
        }
        *error = errorText(code);
        return false;
    }
    Removal removal;
    if (target.pattern == QStringLiteral("*")) {
        removeEntry({ folder, attributes, 0 }, removal, cancel);
    } else if (isFolder(attributes)) {
        forEachMatch(folder, target.pattern.toStdWString(), target.recursive, &removal.error, cancel, [&removal, &cancel](const Entry &entry) {
            removeEntry(entry, removal, cancel);
        });
        if (!removal.error.isEmpty() && removal.failed == 0) {
            removal.failed = 1;
        }
    }
    if (cancel) {
        *error = QStringLiteral("cancelled");
        return false;
    }
    if (removal.failed > 0) {
        *error = QStringLiteral("%1 item(s) could not be deleted, first %2").arg(removal.failed).arg(removal.error);
        return false;
    }
    return true;
#else
    Q_UNUSED(target)
    Q_UNUSED(cancel)
    *error = QStringLiteral("deleting destinations requires Windows");
    return false;
#endif
}

} // namespace COpyTargets
