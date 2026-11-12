/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "copycontroller.h"
#include "copycredentials.h"
#include "copysettings.h"
#include "robocopyrunner.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QLocale>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <string>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <winnetwk.h>
#endif

namespace {

const QString kManifestFormat = QStringLiteral("c-opy-manifest");
const QString kRobocopyProfilesFormat = QStringLiteral("c-opy-robocopy-profiles");
const QString kRobocopyProfilesFileName = QStringLiteral("robocopy-profiles.json");
const QString kDefaultIdentifier = QStringLiteral("*");
// Destinations of the manifest outputs without their own, ahead of the default rule; the
// cluster file's "manifestDestinations". Stored with the output rules under this identifier.
const QString kManifestIdentifier = QStringLiteral("*manifest");
constexpr int kMaxParallelLimit = 32;

// Groups next to the generated COpySettings entries in the same config file:
// [Clusters][<cluster>] Description, [Clusters][<cluster>][Outputs][<identifier>] Destinations/Account/Node
// and [CurrentOutputs][<identifier>] Destinations/Account/Node for the working mapping.
// [ClusterAccounts][<cluster>] Account holds this computer's account per cluster, also for
// library clusters.
const QString kPresetsGroup = QStringLiteral("Clusters");
const QString kClusterAccountsGroup = QStringLiteral("ClusterAccounts");
const QString kPresetOutputsGroup = QStringLiteral("Outputs");
const QString kCurrentOutputsGroup = QStringLiteral("CurrentOutputs");
const QString kDescriptionKey = QStringLiteral("Description");
// "D:=\\{ip}\d$" per drive (one list entry each), in the cluster group and in [CurrentOutputs].
const QString kDriveSharesKey = QStringLiteral("DriveShares");
const QString kDestinationsKey = QStringLiteral("Destinations");
const QString kAccountKey = QStringLiteral("Account");
const QString kNodeKey = QStringLiteral("Node");
// In [CurrentOutputs]: mappingSignature() of the cluster as it was last applied or saved, to
// tell whether the working mapping was edited since.
const QString kAppliedSignatureKey = QStringLiteral("AppliedClusterSignature");

QString localPathFromInput(const QString &pathOrUrl)
{
    const QString trimmed = pathOrUrl.trimmed();
    if (trimmed.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        return QUrl(trimmed).toLocalFile();
    }
    return trimmed;
}

// robocopy reads a trailing backslash in front of a closing quote as an escaped quote,
// so folder arguments never end with a separator ("D:\" becomes "D:\.").
QString robocopyFolderArgument(const QString &path)
{
    QString native = QDir::toNativeSeparators(path.trimmed());
    while (native.size() > 1 && native.endsWith(QLatin1Char('\\'))
        && !(native.size() == 3 && native.at(1) == QLatin1Char(':'))) {
        native.chop(1);
    }
    if (native.endsWith(QLatin1Char('\\'))) {
        native += QLatin1Char('.');
    }
    return native;
}

// Mirroring purges everything in the destination that is not in the source, so it never
// targets a drive root ("D:\") or a share root ("\\host\share").
bool isDriveOrShareRoot(const QString &path)
{
    static const QRegularExpression root(QStringLiteral("^([A-Za-z]:|\\\\\\\\[^\\\\]+\\\\[^\\\\]+)\\\\*$"));
    return root.match(QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed())))).hasMatch();
}

#ifdef Q_OS_WIN
QString windowsFilePath(const QString &path)
{
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (native.startsWith(QStringLiteral("\\\\?\\"))) {
        return native;
    }
    if (native.startsWith(QStringLiteral("\\\\"))) {
        return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    }
    return QStringLiteral("\\\\?\\") + native;
}
#endif

QString resolvedExistingPath(const QFileInfo &info)
{
#ifdef Q_OS_WIN
    // QFileInfo::canonicalFilePath() can retain junction components on Windows.
    const QString native = windowsFilePath(info.absoluteFilePath());
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        const DWORD length = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        std::wstring buffer(length, L'\0');
        const DWORD written = length > 0
            ? GetFinalPathNameByHandleW(handle, buffer.data(), length, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS) : 0;
        CloseHandle(handle);
        if (written > 0 && written < length) {
            QString result = QString::fromWCharArray(buffer.data(), written);
            if (result.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive)) {
                result = QStringLiteral("\\\\") + result.mid(8);
            } else if (result.startsWith(QStringLiteral("\\\\?\\"))) {
                result = result.mid(4);
            }
            return QDir::fromNativeSeparators(result);
        }
    }
#endif
    return info.canonicalFilePath();
}

#ifdef Q_OS_WIN
// A share or a mapped network drive as \\host\share\..., without contacting the host. The
// handle-based resolution blocks for the SMB timeout per ancestor on an unreachable node,
// and resolves nothing beyond the UNC form on a reachable one.
QString remotePath(const QString &absolute)
{
    if (absolute.startsWith(QStringLiteral("//"))) {
        return absolute;
    }
    if (absolute.size() < 2 || absolute.at(1) != QLatin1Char(':')) {
        return QString();
    }
    const std::wstring drive = absolute.left(2).toStdWString();
    const std::wstring root = drive + L"\\";
    if (GetDriveTypeW(root.c_str()) != DRIVE_REMOTE) {
        return QString();
    }
    // Reads the local connection table, also for a disconnected drive.
    wchar_t remote[MAX_PATH + 1] = {};
    DWORD length = MAX_PATH;
    const DWORD status = WNetGetConnectionW(drive.c_str(), remote, &length);
    if (status != NO_ERROR && status != ERROR_CONNECTION_UNAVAIL) {
        return absolute;
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(remote)) + absolute.mid(2));
}
#endif

// Resolve existing ancestors too: a destination need not exist yet, and its parent may
// be a junction. Keep comparisons independent of separators, trailing slashes and case.
QString comparisonPath(const QString &path)
{
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    const QString remote = remotePath(absolute);
    if (!remote.isEmpty()) {
        return remote.toCaseFolded();
    }
#endif
    QString ancestor = absolute;
    QStringList suffix;
    QString resolved;
    while (resolved.isEmpty()) {
        const QFileInfo info(ancestor);
        resolved = resolvedExistingPath(info);
        if (!resolved.isEmpty() || info.absolutePath() == ancestor) {
            break;
        }
        suffix.prepend(info.fileName());
        ancestor = info.absolutePath();
    }
    QString result = resolved.isEmpty() ? absolute : QDir::cleanPath(QDir(resolved).filePath(suffix.join(QLatin1Char('/'))));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

bool isSameOrChildPath(const QString &path, const QString &parent)
{
    return path == parent || path.startsWith(parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/'));
}

QStringList copyKey(const QString &source, const QString &destination, const QString &pattern, bool mirror)
{
    return { comparisonPath(source), comparisonPath(destination), pattern.toCaseFolded(), mirror ? QStringLiteral("mirror") : QStringLiteral("file") };
}

// Key of a source file, for both the scanned paths and the full paths robocopy lists (/FP).
QString scanKey(const QString &path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    return clean.toCaseFolded();
#else
    return clean;
#endif
}

// Average throughput window; long enough to even out file boundaries, short enough to
// follow a change of speed.
constexpr qint64 kRateWindowMs = 20000;

// C-Opy decides about mirroring itself: /MIR for folders, nothing that purges for single files.
QStringList profileArguments(const QString &arguments)
{
    QStringList result;
    for (const QString &argument : QProcess::splitCommand(arguments)) {
        const QString upper = argument.toUpper();
        if (upper != QStringLiteral("/MIR") && upper != QStringLiteral("/PURGE")) {
            result << argument;
        }
    }
    return result;
}

// /MT copies several folders at once, so robocopy lists their files interleaved.
bool usesMultithreading(const QStringList &arguments)
{
    return std::any_of(arguments.cbegin(), arguments.cend(), [](const QString &argument) {
        return argument.compare(QStringLiteral("/MT"), Qt::CaseInsensitive) == 0
            || argument.startsWith(QStringLiteral("/MT:"), Qt::CaseInsensitive);
    });
}

// "D", "d:", "D:\\" or "D:/folder" -> "D:"; empty when there is no drive letter.
QString normalizedDrive(const QString &pathOrDrive)
{
    const QString trimmed = pathOrDrive.trimmed();
    if (trimmed.size() >= 1 && trimmed.at(0).isLetter() && (trimmed.size() == 1 || trimmed.at(1) == QLatin1Char(':'))) {
        return trimmed.left(1).toUpper() + QLatin1Char(':');
    }
    return QString();
}

QMap<QString, QString> driveSharesFromJson(const QJsonObject &object)
{
    QMap<QString, QString> result;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString drive = normalizedDrive(it.key());
        const QString share = it.value().toString().trimmed();
        if (!drive.isEmpty() && !share.isEmpty()) {
            result.insert(drive, share);
        }
    }
    return result;
}

QMap<QString, QString> readDriveShares(const KConfigGroup &group)
{
    QMap<QString, QString> result;
    for (const QString &entry : group.readEntry(kDriveSharesKey, QStringList())) {
        const qsizetype separator = entry.indexOf(QLatin1Char('='));
        const QString drive = normalizedDrive(entry.left(separator));
        if (separator > 0 && !drive.isEmpty() && !entry.mid(separator + 1).trimmed().isEmpty()) {
            result.insert(drive, entry.mid(separator + 1).trimmed());
        }
    }
    return result;
}

void writeDriveShares(KConfigGroup group, const QMap<QString, QString> &shares)
{
    QStringList entries;
    for (auto it = shares.cbegin(); it != shares.cend(); ++it) {
        entries << it.key() + QLatin1Char('=') + it.value();
    }
    if (!entries.isEmpty()) {
        group.writeEntry(kDriveSharesKey, entries);
    }
}

QString robocopyExitText(int exitCode)
{
    if (exitCode < 0) {
        return QStringLiteral("robocopy did not run");
    }
    if (exitCode >= 16) {
        return QStringLiteral("fatal error, nothing copied");
    }
    if (exitCode >= 8) {
        return QStringLiteral("some files could not be copied");
    }
    if (exitCode == 0) {
        return QStringLiteral("already up to date");
    }
    QStringList parts;
    if (exitCode & 1) parts << QStringLiteral("files copied");
    if (exitCode & 2) parts << QStringLiteral("extra files at destination");
    if (exitCode & 4) parts << QStringLiteral("mismatched files at destination");
    return parts.join(QStringLiteral(", "));
}

QStringList nonEmptyEntries(const QStringList &entries)
{
    QStringList result;
    for (const QString &entry : entries) {
        if (!entry.trimmed().isEmpty()) {
            result << entry.trimmed();
        }
    }
    return result;
}

QMap<QString, QStringList> normalizedDestinations(const QMap<QString, QStringList> &destinations)
{
    QMap<QString, QStringList> result;
    for (auto it = destinations.cbegin(); it != destinations.cend(); ++it) {
        const QStringList entries = nonEmptyEntries(it.value());
        if (!entries.isEmpty()) {
            result.insert(it.key(), entries);
        }
    }
    return result;
}

QMap<QString, QStringList> destinationsFromJson(const QJsonObject &object)
{
    QMap<QString, QStringList> result;
    for (auto it = object.begin(); it != object.end(); ++it) {
        QStringList entries;
        if (it.value().isString()) {
            entries << it.value().toString();
        }
        for (const QJsonValue &value : it.value().toArray()) {
            entries << value.toString();
        }
        entries = nonEmptyEntries(entries);
        if (!entries.isEmpty()) {
            result.insert(it.key(), entries);
        }
    }
    return result;
}

// Output identifier -> account or node name. Accounts are referenced by name only; passwords
// stay in the Windows Credential Manager. Nodes are resolved from the node folder when copying.
QMap<QString, QString> namesFromJson(const QJsonObject &object)
{
    QMap<QString, QString> result;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString name = it.value().toString().trimmed();
        if (!name.isEmpty()) {
            result.insert(it.key(), name);
        }
    }
    return result;
}

// One subgroup per output identifier, so identifiers never have to be valid config keys.
void readOutputRules(const KConfigGroup &group, QMap<QString, QStringList> &destinations, QMap<QString, QString> &accounts,
    QMap<QString, QString> &nodes)
{
    destinations.clear();
    accounts.clear();
    nodes.clear();
    for (const QString &identifier : group.groupList()) {
        const KConfigGroup rule = group.group(identifier);
        const QStringList entries = nonEmptyEntries(rule.readEntry(kDestinationsKey, QStringList()));
        if (!entries.isEmpty()) {
            destinations.insert(identifier, entries);
        }
        const QString account = rule.readEntry(kAccountKey, QString()).trimmed();
        if (!account.isEmpty()) {
            accounts.insert(identifier, account);
        }
        const QString node = rule.readEntry(kNodeKey, QString()).trimmed();
        if (!node.isEmpty()) {
            nodes.insert(identifier, node);
        }
    }
}

void writeOutputRules(KConfigGroup group, const QMap<QString, QStringList> &destinations, const QMap<QString, QString> &accounts,
    const QMap<QString, QString> &nodes)
{
    const QMap<QString, QStringList> normalized = normalizedDestinations(destinations);
    for (auto it = normalized.cbegin(); it != normalized.cend(); ++it) {
        group.group(it.key()).writeEntry(kDestinationsKey, it.value());
    }
    for (auto it = accounts.cbegin(); it != accounts.cend(); ++it) {
        group.group(it.key()).writeEntry(kAccountKey, it.value());
    }
    for (auto it = nodes.cbegin(); it != nodes.cend(); ++it) {
        group.group(it.key()).writeEntry(kNodeKey, it.value());
    }
}

// A hash of a mapping's destinations, accounts, nodes and drive shares.
QString mappingSignature(const QMap<QString, QStringList> &destinations, const QMap<QString, QString> &accounts,
    const QMap<QString, QString> &nodes, const QMap<QString, QString> &driveShares)
{
    const auto names = [](const QMap<QString, QString> &map) {
        QJsonObject object;
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            object.insert(it.key(), it.value());
        }
        return object;
    };
    QJsonObject destinationObject;
    const QMap<QString, QStringList> normalized = normalizedDestinations(destinations);
    for (auto it = normalized.cbegin(); it != normalized.cend(); ++it) {
        destinationObject.insert(it.key(), QJsonArray::fromStringList(it.value()));
    }
    const QJsonObject root {
        { QStringLiteral("destinations"), destinationObject },
        { QStringLiteral("accounts"), names(accounts) },
        { QStringLiteral("nodes"), names(nodes) },
        { QStringLiteral("driveShares"), names(driveShares) },
    };
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex());
}

// Returns an empty object and sets *error when the file cannot be read or holds no JSON object.
QJsonObject readJsonObject(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return QJsonObject();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = parseError.errorString();
        return QJsonObject();
    }
    if (!document.isObject()) {
        *error = QStringLiteral("not a JSON object");
        return QJsonObject();
    }
    return document.object();
}

bool writeJsonObject(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

// A cluster file with a "clusters" array (or the older "presets" array).
bool hasClusters(const QJsonObject &root)
{
    return root.contains(QStringLiteral("clusters")) || root.contains(QStringLiteral("presets"));
}

// All *.json files in a folder and its subfolders, in a stable order.
QStringList jsonFilesIn(const QString &folder)
{
    QStringList files;
    QDirIterator it(folder, { QStringLiteral("*.json") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files << it.next();
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

QString localFolder(const QUrl &url, bool isFile)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    return QDir::toNativeSeparators(isFile ? info.absolutePath() : info.absoluteFilePath());
}

QUrl dialogFolderUrl(const QString &lastFolder, const QUrl &fallback = QUrl())
{
    return lastFolder.isEmpty() ? fallback : QUrl::fromLocalFile(QDir::fromNativeSeparators(lastFolder));
}

} // namespace

QSharedPointer<const COpyController::SourceScan> COpyController::scanSource(const QString &source, const QString &pattern, bool recursive, bool skipHidden, bool indexFiles)
{
    auto scan = QSharedPointer<SourceScan>::create();
    // Same order as robocopy, so a listed file marks every file before it as passed.
    QStringList folders { source };
    while (!folders.isEmpty()) {
        const QString folder = folders.takeLast();
        QDirIterator files(folder, { pattern }, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks);
        while (files.hasNext()) {
            const QFileInfo info = files.nextFileInfo();
            bool excluded = info.isJunction();
            if (skipHidden) {
#ifdef Q_OS_WIN
                // QDir::System means special entries, not FILE_ATTRIBUTE_SYSTEM.
                const QString native = windowsFilePath(info.filePath());
                const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
                excluded |= attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
#else
                excluded |= info.isHidden();
#endif
            }
            if (!excluded) {
                // The size comes with the directory listing; no extra file access.
                scan->totalBytes += info.size();
                if (indexFiles) {
                    scan->fileIndex.insert(scanKey(info.filePath()), scan->fileCount);
                    scan->fileEnds << scan->totalBytes;
                }
                ++scan->fileCount;
            }
        }
        if (recursive) {
            // /XA:SH filters file attributes, not the attributes of their parent folders.
            QStringList subfolders;
            QDirIterator dirs(folder, QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            while (dirs.hasNext()) {
                const QFileInfo info = dirs.nextFileInfo();
                if (!info.isJunction()) {
                    subfolders << info.filePath();
                }
            }
            // Reversed onto the stack, so the first subfolder is visited next.
            std::reverse(subfolders.begin(), subfolders.end());
            folders << subfolders;
        }
    }
    return scan;
}

COpyController::COpyController(QObject *parent)
    : QObject(parent)
{
    // Robocopy reports progress many times per second; coalesce updates for QML.
    m_tasksChangedTimer.setSingleShot(true);
    m_tasksChangedTimer.setInterval(100);
    connect(&m_tasksChangedTimer, &QTimer::timeout, this, &COpyController::tasksChanged);
    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &COpyController::sampleCopyStats);
    // The planned copies follow the sources, the mapping, the nodes and the profiles.
    for (const auto signal : { &COpyController::manifestsChanged, &COpyController::destinationsChanged, &COpyController::nodesChanged,
             &COpyController::localNodeChanged, &COpyController::robocopyProfilesChanged }) {
        connect(this, signal, this, &COpyController::plannedCopiesChanged);
    }
    connect(this, &COpyController::samePathChanged, this, &COpyController::plannedCopiesChanged);
    // targetsStale() compares the checked targets with the plan.
    connect(this, &COpyController::plannedCopiesChanged, this, &COpyController::targetsChanged);
    m_targetPool.setMaxThreadCount(16);

    appendLog(QStringLiteral("C-Opy started. Settings: %1").arg(QDir::toNativeSeparators(settingsFilePath())));
    loadSettings();
    loadNodes();
    loadRobocopyProfiles();
    refreshAccounts();
    const auto libraryPresets = std::count_if(m_presets.cbegin(), m_presets.cend(), [](const Preset &preset) {
        return !preset.filePath.isEmpty();
    });
    appendLog(QStringLiteral("Library: %1 cluster(s) from %2, %3 node(s) from %4.")
        .arg(libraryPresets)
        .arg(presetFolder())
        .arg(m_nodes.size())
        .arg(nodeFolder()));
    applyStartupOptions();
    const QString local = localNode();
    appendLog(local.isEmpty() ? QStringLiteral("This computer (%1) is not one of the nodes.").arg(hostName())
                              : QStringLiteral("This computer (%1) is node '%2'%3.")
                                    .arg(hostName(), local,
                                        skipLocalNode() ? QStringLiteral("; it is skipped when copying to its cluster") : QString()));
}

COpyController::~COpyController()
{
    // Checks and deletes post their results to this object; let them end first.
    m_targetsCancelled->store(true);
    m_targetPool.waitForDone();
    for (CopyTask &task : m_tasks) {
        if (task.process) {
            // The runner terminates its robocopy process and waits for it.
            task.process->disconnect(this);
            delete task.process;
            task.process = nullptr;
        }
    }
}

QVariantList COpyController::manifests() const
{
    QVariantList result;
    for (const Manifest &manifest : m_manifests) {
        QStringList identifiers;
        for (const ManifestOutput &output : manifest.outputs) {
            identifiers << output.identifier;
        }
        QString kind = QStringLiteral("manifest");
        if (manifest.kind == SourceKind::File) {
            kind = QStringLiteral("file");
        } else if (manifest.kind == SourceKind::Folder) {
            kind = QStringLiteral("folder");
        }
        result << QVariantMap {
            { QStringLiteral("id"), manifest.id },
            { QStringLiteral("kind"), kind },
            { QStringLiteral("path"), QDir::toNativeSeparators(manifest.path) },
            { QStringLiteral("jobName"), manifest.jobName },
            { QStringLiteral("generator"), manifest.generator },
            { QStringLiteral("created"), manifest.created },
            { QStringLiteral("outputRoot"), QDir::toNativeSeparators(manifest.outputRoot) },
            { QStringLiteral("identifiers"), identifiers.join(QStringLiteral(", ")) },
            { QStringLiteral("status"), manifestStatus(manifest) },
            { QStringLiteral("busy"), manifestBusy(manifest.id) },
        };
    }
    return result;
}

int COpyController::readyManifestCount() const
{
    return static_cast<int>(std::count_if(m_manifests.cbegin(), m_manifests.cend(), [](const Manifest &manifest) {
        return !manifest.queued;
    }));
}

QVariantList COpyController::destinations() const
{
    // The default rule comes first, then every output in the loaded manifests, then any
    // other identifiers the current mapping knows about (e.g. from a cluster).
    QStringList identifiers { kDefaultIdentifier };
    QMap<QString, QString> sources;
    for (const Manifest &manifest : m_manifests) {
        if (manifest.kind != SourceKind::Manifest) {
            // Files and folders go to every cluster node, not to an output rule.
            continue;
        }
        for (const ManifestOutput &output : manifest.outputs) {
            if (!identifiers.contains(output.identifier)) {
                identifiers << output.identifier;
            }
            sources.insert(output.identifier,
                QStringLiteral("%1 \u00b7 %2").arg(QDir::toNativeSeparators(output.sourceDirectory), output.filePattern));
        }
    }
    for (const QString &identifier : m_destinations.keys() + m_accounts.keys() + m_outputNodes.keys()) {
        if (!identifiers.contains(identifier) && identifier != kManifestIdentifier) {
            identifiers << identifier;
        }
    }
    // The manifest destination always follows the default rule, so it can be filled in.
    identifiers.insert(1, kManifestIdentifier);

    const bool hasDefault = !nonEmptyEntries(m_destinations.value(kDefaultIdentifier)).isEmpty();
    const bool hasManifestDefault = !nonEmptyEntries(m_destinations.value(kManifestIdentifier)).isEmpty();
    const QStringList storedNames = storedAccountNames();
    QVariantList result;
    for (const QString &identifier : identifiers) {
        const bool isDefault = identifier == kDefaultIdentifier;
        const bool isManifestDefault = identifier == kManifestIdentifier;
        const bool isOutput = !isDefault && !isManifestDefault;
        const QStringList entries = m_destinations.value(identifier);
        const QString account = m_accounts.value(identifier);
        // What the rule copies with when it has no account of its own.
        const QString inheritedAccount = isDefault ? settingsAccount() : defaultRuleAccount();
        const QString effectiveAccount = account.isEmpty() ? inheritedAccount : account;
        const QString effectiveNode = effectiveNodeName(identifier);
        const Node *node = findNode(effectiveNode);
        result << QVariantMap {
            { QStringLiteral("identifier"), identifier },
            { QStringLiteral("isDefault"), isDefault },
            { QStringLiteral("isManifestDefault"), isManifestDefault },
            { QStringLiteral("manifestDefaultSet"), hasManifestDefault },
            { QStringLiteral("inManifest"), sources.contains(identifier) },
            { QStringLiteral("source"), sources.value(identifier) },
            { QStringLiteral("destinations"), entries },
            { QStringLiteral("usesDefault"), !isDefault && hasDefault && nonEmptyEntries(entries).isEmpty() && !(isOutput && hasManifestDefault) },
            { QStringLiteral("usesManifestDefault"), isOutput && hasManifestDefault && nonEmptyEntries(entries).isEmpty() },
            { QStringLiteral("account"), account },
            { QStringLiteral("inheritedAccount"), inheritedAccount },
            { QStringLiteral("effectiveAccount"), effectiveAccount },
            { QStringLiteral("accountMissing"), !effectiveAccount.isEmpty() && !storedNames.contains(effectiveAccount) },
            { QStringLiteral("node"), m_outputNodes.value(identifier) },
            { QStringLiteral("effectiveNode"), effectiveNode },
            { QStringLiteral("nodeAddress"), node ? node->ipAddress : QString() },
            { QStringLiteral("nodeMissing"), !effectiveNode.isEmpty() && !node },
        };
    }
    return result;
}

QStringList COpyController::presetNames() const
{
    QStringList names;
    for (const Preset &preset : m_presets) {
        names << preset.name;
    }
    return names;
}

QVariantList COpyController::presets() const
{
    QVariantList result;
    for (const Preset &preset : m_presets) {
        result << QVariantMap {
            { QStringLiteral("name"), preset.name },
            { QStringLiteral("description"), preset.description },
            { QStringLiteral("library"), !preset.filePath.isEmpty() },
            { QStringLiteral("filePath"), QDir::toNativeSeparators(preset.filePath) },
            { QStringLiteral("nodes"), presetClusterNodes(preset.nodes) },
        };
    }
    return result;
}

QString COpyController::currentPreset() const
{
    return m_currentPreset;
}

bool COpyController::currentPresetIsLibrary() const
{
    const int index = presetIndex(m_currentPreset);
    return index >= 0 && !m_presets.at(index).filePath.isEmpty();
}

QString COpyController::presetFolder() const
{
    const QString folder = COpySettings::presetFolder().trimmed();
    return folder.isEmpty() ? defaultPresetFolder() : QDir::toNativeSeparators(folder);
}

void COpyController::setPresetFolder(const QString &folder)
{
    QString value = QDir::toNativeSeparators(localPathFromInput(folder));
    if (value == defaultPresetFolder()) {
        value.clear();
    }
    if (value == COpySettings::presetFolder()) {
        return;
    }
    COpySettings::setPresetFolder(value);
    COpySettings::self()->save();
    Q_EMIT libraryFoldersChanged();
    loadLibraryPresets();
}

QString COpyController::nodeFolder() const
{
    const QString folder = COpySettings::nodeFolder().trimmed();
    return folder.isEmpty() ? defaultNodeFolder() : QDir::toNativeSeparators(folder);
}

void COpyController::setNodeFolder(const QString &folder)
{
    QString value = QDir::toNativeSeparators(localPathFromInput(folder));
    if (value == defaultNodeFolder()) {
        value.clear();
    }
    if (value == COpySettings::nodeFolder()) {
        return;
    }
    COpySettings::setNodeFolder(value);
    COpySettings::self()->save();
    Q_EMIT libraryFoldersChanged();
    loadNodes();
}

QString COpyController::defaultPresetFolder() const
{
    return QDir::toNativeSeparators(QDir(presetExamplesFolder().toLocalFile()).filePath(QStringLiteral("clusters")));
}

QString COpyController::defaultNodeFolder() const
{
    // Node addresses differ per machine, so they live next to the settings file, not in the installation.
    return QDir::toNativeSeparators(QDir(QFileInfo(settingsFilePath()).absolutePath()).filePath(QStringLiteral("nodes")));
}

QVariantList COpyController::nodes() const
{
    QVariantList result;
    for (const Node &node : m_nodes) {
        result << QVariantMap {
            { QStringLiteral("name"), node.name },
            { QStringLiteral("ipAddress"), node.ipAddress },
            { QStringLiteral("description"), node.description },
            { QStringLiteral("filePath"), QDir::toNativeSeparators(node.filePath) },
        };
    }
    return result;
}

QStringList COpyController::nodeNames() const
{
    QStringList names;
    for (const Node &node : m_nodes) {
        names << node.name;
    }
    return names;
}

bool COpyController::presetModified() const
{
    const int index = presetIndex(m_currentPreset);
    if (index < 0) {
        return false;
    }
    const Preset &preset = m_presets.at(index);
    return preset.accounts != m_accounts
        || preset.nodes != m_outputNodes
        || preset.driveShares != m_driveShares
        || normalizedDestinations(preset.destinations) != normalizedDestinations(m_destinations);
}

QStringList COpyController::clusterNodes() const
{
    return presetClusterNodes(m_outputNodes);
}

QStringList COpyController::presetClusterNodes(const NodeMap &nodes)
{
    QStringList names;
    for (const QString &name : nodes) {
        if (!names.contains(name, Qt::CaseInsensitive)) {
            names << name;
        }
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

QString COpyController::localNode() const
{
    const QString configured = localNodeSetting();
    if (configured.isEmpty()) {
        return m_detectedLocalNode;
    }
    // A configured node that is no longer in the node folder still counts, so it is never copied to.
    const Node *node = findNode(configured);
    return node ? node->name : configured;
}

QString COpyController::detectedLocalNode() const
{
    return m_detectedLocalNode;
}

QString COpyController::localNodeSetting() const
{
    return COpySettings::localNode().trimmed();
}

void COpyController::setLocalNodeSetting(const QString &name)
{
    if (name.trimmed() == localNodeSetting()) {
        return;
    }
    COpySettings::setLocalNode(name.trimmed());
    COpySettings::self()->save();
    Q_EMIT localNodeChanged();
}

bool COpyController::skipLocalNode() const
{
    return COpySettings::skipLocalNode();
}

void COpyController::setSkipLocalNode(bool skip)
{
    if (skip == skipLocalNode()) {
        return;
    }
    COpySettings::setSkipLocalNode(skip);
    COpySettings::self()->save();
    Q_EMIT localNodeChanged();
}

bool COpyController::skipHiddenFiles() const
{
    return COpySettings::skipHiddenFiles();
}

void COpyController::setSkipHiddenFiles(bool skip)
{
    if (skip == skipHiddenFiles()) {
        return;
    }
    COpySettings::setSkipHiddenFiles(skip);
    COpySettings::self()->save();
    Q_EMIT skipHiddenFilesChanged();
}

QString COpyController::hostName() const
{
    return QHostInfo::localHostName();
}

QString COpyController::startupCluster() const
{
    return COpySettings::startupCluster();
}

void COpyController::setStartupCluster(const QString &name)
{
    if (name == startupCluster()) {
        return;
    }
    COpySettings::setStartupCluster(name);
    COpySettings::self()->save();
    Q_EMIT startupOptionsChanged();
}

QVariantList COpyController::robocopyProfiles() const
{
    QVariantList result;
    for (const RobocopyProfile &profile : m_robocopyProfiles) {
        result << QVariantMap {
            { QStringLiteral("name"), profile.name },
            { QStringLiteral("arguments"), profile.arguments },
            { QStringLiteral("description"), profile.description },
        };
    }
    return result;
}

QStringList COpyController::robocopyProfileNames() const
{
    QStringList names;
    for (const RobocopyProfile &profile : m_robocopyProfiles) {
        names << profile.name;
    }
    return names;
}

QVariantList COpyController::defaultRobocopyProfiles() const
{
    QVariantList result;
    for (const RobocopyProfile &profile : builtInRobocopyProfiles()) {
        result << QVariantMap {
            { QStringLiteral("name"), profile.name },
            { QStringLiteral("arguments"), profile.arguments },
            { QStringLiteral("description"), profile.description },
        };
    }
    return result;
}

QString COpyController::fileRobocopyProfile() const
{
    return robocopyProfileFor(true).name;
}

void COpyController::setFileRobocopyProfile(const QString &name)
{
    if (name == COpySettings::fileRobocopyProfile() || !findRobocopyProfile(name)) {
        return;
    }
    COpySettings::setFileRobocopyProfile(name);
    COpySettings::self()->save();
    Q_EMIT robocopyProfilesChanged();
}

QString COpyController::folderRobocopyProfile() const
{
    return robocopyProfileFor(false).name;
}

void COpyController::setFolderRobocopyProfile(const QString &name)
{
    if (name == COpySettings::folderRobocopyProfile() || !findRobocopyProfile(name)) {
        return;
    }
    COpySettings::setFolderRobocopyProfile(name);
    COpySettings::self()->save();
    Q_EMIT robocopyProfilesChanged();
}

QString COpyController::robocopyProfilesFilePath() const
{
    return QDir::toNativeSeparators(QDir(QFileInfo(settingsFilePath()).absolutePath()).filePath(kRobocopyProfilesFileName));
}

void COpyController::setRobocopyProfiles(const QVariantList &profiles)
{
    QList<RobocopyProfile> result;
    for (const QVariant &value : profiles) {
        const QVariantMap map = value.toMap();
        RobocopyProfile profile;
        profile.name = map.value(QStringLiteral("name")).toString().trimmed();
        profile.arguments = map.value(QStringLiteral("arguments")).toString().simplified();
        profile.description = map.value(QStringLiteral("description")).toString().trimmed();
        const bool duplicate = std::any_of(result.cbegin(), result.cend(), [&profile](const RobocopyProfile &other) {
            return other.name.compare(profile.name, Qt::CaseInsensitive) == 0;
        });
        if (profile.name.isEmpty() || duplicate) {
            appendLog(profile.name.isEmpty() ? QStringLiteral("Skipped a robocopy profile without a name.")
                                             : QStringLiteral("Skipped the second robocopy profile named '%1'.").arg(profile.name));
            continue;
        }
        result << profile;
    }
    if (result.isEmpty()) {
        appendLog(QStringLiteral("At least one robocopy profile is needed; the profiles were not changed."));
        Q_EMIT robocopyProfilesChanged();
        return;
    }
    m_robocopyProfiles = result;
    if (saveRobocopyProfiles()) {
        appendLog(QStringLiteral("Saved %1 robocopy profile(s) to %2.").arg(m_robocopyProfiles.size()).arg(robocopyProfilesFilePath()));
    }
    Q_EMIT robocopyProfilesChanged();
}

QString COpyController::sourceMode() const
{
    return m_sourceMode;
}

void COpyController::setSourceMode(const QString &mode)
{
    if (mode == m_sourceMode) {
        return;
    }
    m_sourceMode = mode;
    Q_EMIT sourceModeChanged();
}

bool COpyController::samePath() const
{
    return COpySettings::copyToSamePath();
}

void COpyController::setSamePath(bool samePath)
{
    if (samePath == this->samePath()) {
        return;
    }
    COpySettings::setCopyToSamePath(samePath);
    COpySettings::self()->save();
    Q_EMIT samePathChanged();
}

QVariantList COpyController::driveShares() const
{
    QVariantList result;
    for (auto it = m_driveShares.cbegin(); it != m_driveShares.cend(); ++it) {
        result << QVariantMap {
            { QStringLiteral("drive"), it.key() },
            { QStringLiteral("share"), it.value() },
        };
    }
    return result;
}

QStringList COpyController::unsharedDrives() const
{
    QStringList drives;
    for (const Manifest &manifest : m_manifests) {
        if (manifest.queued || manifest.kind == SourceKind::Manifest) {
            continue;
        }
        const QString drive = normalizedDrive(manifest.path);
        if (!drive.isEmpty() && !m_driveShares.contains(drive) && !drives.contains(drive)) {
            drives << drive;
        }
    }
    drives.sort();
    return drives;
}

void COpyController::setDriveShare(const QString &drive, const QString &share)
{
    const QString key = normalizedDrive(drive);
    const QString value = share.trimmed();
    if (key.isEmpty() || m_driveShares.value(key) == value) {
        return;
    }
    if (value.isEmpty()) {
        m_driveShares.remove(key);
    } else {
        m_driveShares.insert(key, value);
    }
    destinationsEdited();
}

QVariantList COpyController::plannedCopies() const
{
    QVariantList result;
    for (const PlannedCopy &copy : planCopies()) {
        result << QVariantMap {
            { QStringLiteral("jobName"), copy.jobName },
            { QStringLiteral("identifier"), copy.identifier },
            { QStringLiteral("node"), copy.node },
            { QStringLiteral("destination"), QDir::toNativeSeparators(copy.destination) },
            { QStringLiteral("accountName"), copy.accountName },
            { QStringLiteral("profileName"), copy.profileName },
            { QStringLiteral("mirror"), copy.mirror },
            { QStringLiteral("skipped"), copy.skipped },
            { QStringLiteral("local"), copy.local },
            { QStringLiteral("key"), plannedCopyKey(copy) },
            { QStringLiteral("disabled"), copy.disabled },
        };
    }
    return result;
}

int COpyController::maxParallel() const
{
    return m_maxParallel;
}

void COpyController::setMaxParallel(int maxParallel)
{
    const int value = std::clamp(maxParallel, 1, kMaxParallelLimit);
    if (m_maxParallel == value) {
        return;
    }
    m_maxParallel = value;
    Q_EMIT maxParallelChanged();
    saveSettings();
    pumpQueue();
}

bool COpyController::autoStart() const
{
    return m_autoStart;
}

void COpyController::setAutoStart(bool autoStart)
{
    if (m_autoStart == autoStart) {
        return;
    }
    m_autoStart = autoStart;
    Q_EMIT autoStartChanged();
    saveSettings();
}

QVariantList COpyController::tasks() const
{
    QVariantList result;
    for (const CopyTask &task : m_tasks) {
        QString status;
        double progress = 0.0;
        switch (task.status) {
        case TaskStatus::Pending:
            status = QStringLiteral("Pending");
            break;
        case TaskStatus::Running:
            status = QStringLiteral("Running");
            // By bytes, so large files weigh more; files robocopy skips as unchanged count as passed.
            if (task.totalBytes > 0) {
                progress = std::min(1.0, static_cast<double>(taskProcessedBytes(task)) / task.totalBytes);
            } else if (task.totalFiles > 0) {
                progress = std::min(1.0, (std::max(0, task.filesStarted - 1) + task.fileProgress) / task.totalFiles);
            } else {
                progress = task.fileProgress;
            }
            break;
        case TaskStatus::Done:
            status = QStringLiteral("Done");
            progress = 1.0;
            break;
        case TaskStatus::Failed:
            status = QStringLiteral("Failed");
            break;
        case TaskStatus::Stopped:
            status = QStringLiteral("Stopped");
            break;
        }
        const bool active = task.status == TaskStatus::Running && task.process;
        // Running: the recent speed. Finished: the average over the whole copy.
        double rate = -1.0;
        if (active) {
            rate = taskRate(task);
        } else if (!task.process && task.elapsedMs > 0) {
            rate = taskCopiedBytes(task) * 1000.0 / task.elapsedMs;
        }
        const qint64 remainingBytes = active ? taskRemainingBytes(task) : -1;
        double remainingSeconds = -1.0;
        if (remainingBytes == 0) {
            remainingSeconds = 0.0;
        } else if (remainingBytes > 0 && rate > 0.0) {
            remainingSeconds = remainingBytes / rate;
        }
        result << QVariantMap {
            { QStringLiteral("id"), task.id },
            { QStringLiteral("jobName"), task.jobName },
            { QStringLiteral("identifier"), task.identifier },
            { QStringLiteral("source"), QDir::toNativeSeparators(task.source) },
            { QStringLiteral("destination"), QDir::toNativeSeparators(task.destination) },
            { QStringLiteral("filePattern"), task.filePattern },
            { QStringLiteral("accountName"), task.accountName },
            { QStringLiteral("profileName"), task.profileName },
            { QStringLiteral("status"), status },
            { QStringLiteral("progress"), progress },
            { QStringLiteral("totalFiles"), task.totalFiles },
            { QStringLiteral("filesStarted"), task.filesStarted },
            { QStringLiteral("totalBytes"), task.totalBytes },
            { QStringLiteral("copiedBytes"), taskCopiedBytes(task) },
            { QStringLiteral("elapsedSeconds"), (task.process ? task.clock.elapsed() : task.elapsedMs) / 1000.0 },
            { QStringLiteral("remainingSeconds"), remainingSeconds },
            { QStringLiteral("bytesPerSecond"), rate },
            { QStringLiteral("estimatesTime"), !task.multithreaded },
            { QStringLiteral("message"), task.message },
        };
    }
    return result;
}

int COpyController::failedTaskCount() const
{
    return static_cast<int>(std::count_if(m_tasks.cbegin(), m_tasks.cend(), [](const CopyTask &task) {
        return task.status == TaskStatus::Failed || task.status == TaskStatus::Stopped;
    }));
}

int COpyController::finishedTaskCount() const
{
    return static_cast<int>(std::count_if(m_tasks.cbegin(), m_tasks.cend(), [](const CopyTask &task) {
        return task.status != TaskStatus::Pending && task.status != TaskStatus::Running && !task.process;
    }));
}

bool COpyController::running() const
{
    return m_running;
}

double COpyController::elapsedSeconds() const
{
    return (m_running ? m_runClock.elapsed() : m_runElapsedMs) / 1000.0;
}

double COpyController::remainingSeconds() const
{
    return m_remainingSeconds;
}

double COpyController::copyRate() const
{
    return m_copyRate;
}

qint64 COpyController::copiedBytes() const
{
    return m_runCopiedBytes;
}

QString COpyController::formatDuration(double seconds) const
{
    const qint64 total = std::max<qint64>(0, qRound64(seconds));
    const qint64 hours = total / 3600;
    const qint64 minutes = total / 60 % 60;
    const QString secondsText = QStringLiteral("%1").arg(total % 60, 2, 10, QLatin1Char('0'));
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(secondsText);
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(secondsText);
}

QString COpyController::formatDataSize(qint64 bytes) const
{
    return QLocale().formattedDataSize(bytes);
}

QString COpyController::logText() const
{
    return m_logText;
}

QString COpyController::settingsFilePath() const
{
    // KConfig resolves the relative kcfgfile name against the generic config folder.
    return QDir::toNativeSeparators(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
            .filePath(COpySettings::self()->config()->name()));
}

QUrl COpyController::manifestDialogFolder() const
{
    return dialogFolderUrl(COpySettings::lastManifestDialogLocation(),
        QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)));
}

QUrl COpyController::sourceDialogFolder() const
{
    return dialogFolderUrl(COpySettings::lastSourceDialogLocation(), manifestDialogFolder());
}

QUrl COpyController::presetDialogFolder() const
{
    return dialogFolderUrl(COpySettings::lastPresetDialogLocation(), presetExamplesFolder());
}

QUrl COpyController::destinationDialogFolder() const
{
    return dialogFolderUrl(COpySettings::lastDestinationDialogLocation());
}

bool COpyController::openPath(const QString &pathOrUrl)
{
    return openPaths({ pathOrUrl });
}

bool COpyController::openPaths(const QStringList &paths, const QString &mode)
{
    // Keep the delete guard until the worker has finished touching the old destinations.
    if (std::any_of(m_targets.cbegin(), m_targets.cend(), [](const CheckedTarget &target) {
            return target.action == QStringLiteral("deleting");
        })) {
        appendLog(QStringLiteral("Deleting is still running; open new sources when it has finished."));
        return false;
    }

    QSet<int> selected;
    bool opened = false;
    for (const QString &path : paths) {
        const QFileInfo info(localPathFromInput(path));
        bool isManifest = mode == QStringLiteral("manifest");
        if (mode != QStringLiteral("file") && info.isFile()
            && (isManifest || info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0)) {
            QString error;
            const QJsonObject root = readJsonObject(info.absoluteFilePath(), &error);
            if (root.value(QStringLiteral("format")).toString() == kManifestFormat) {
                isManifest = true;
            } else if (hasClusters(root) || root.contains(QStringLiteral("destinations"))) {
                // Importing a cluster alone does not replace the current source selection.
                opened |= loadManifest(path);
                continue;
            }
        }
        const bool success = isManifest ? loadManifest(path) : addSource(path);
        opened |= success;
        if (!success) {
            continue;
        }
        for (const Manifest &manifest : std::as_const(m_manifests)) {
            if (!manifest.queued && (manifest.kind == SourceKind::Manifest) == isManifest && QFileInfo(manifest.path) == info) {
                selected.insert(manifest.id);
                setSourceMode(manifest.kind == SourceKind::Manifest ? QStringLiteral("manifest")
                    : manifest.kind == SourceKind::Folder           ? QStringLiteral("folder")
                                                                   : QStringLiteral("file"));
            }
        }
    }
    // Invalid inputs and cluster imports alone do not replace the current selection.
    if (selected.isEmpty()) {
        return opened;
    }

    // Copy tasks own their source/destination data and keep running in step 5.
    m_manifests.removeIf([&selected](const Manifest &manifest) { return !selected.contains(manifest.id); });
    m_disabledCopies.clear();
    // Old checks may finish later; target IDs are never reused and their results are ignored.
    m_targetsCancelled->store(true);
    m_targetsCancelled = std::make_shared<std::atomic_bool>(false);
    m_targets.clear();
    Q_EMIT manifestsChanged();
    Q_EMIT destinationsChanged();
    Q_EMIT targetsChanged();
    return opened;
}

bool COpyController::addSource(const QString &pathOrUrl)
{
    const QFileInfo info(localPathFromInput(pathOrUrl));
    const QString path = QDir::cleanPath(info.absoluteFilePath());
    if (!info.exists()) {
        appendLog(QStringLiteral("%1 does not exist.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    if (info.isDir() && info.isRoot()) {
        appendLog(QStringLiteral("%1 is a drive root; open a folder on it instead.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    for (const Manifest &existing : std::as_const(m_manifests)) {
        if (!existing.queued && existing.kind != SourceKind::Manifest && QFileInfo(existing.path) == info) {
            appendLog(QStringLiteral("%1 is already added.").arg(QDir::toNativeSeparators(path)));
            return true;
        }
    }

    Manifest source;
    source.id = ++m_nextManifestId;
    source.kind = info.isDir() ? SourceKind::Folder : SourceKind::File;
    source.path = path;
    source.jobName = info.fileName();
    ManifestOutput output;
    output.identifier = info.fileName();
    output.sourceDirectory = info.isDir() ? path : QDir::cleanPath(info.absolutePath());
    output.filePattern = info.isDir() ? QStringLiteral("*") : info.fileName();
    source.outputs << output;

    m_manifests << source;
    appendLog(QStringLiteral("Added %1 %2; it is copied to every node of the cluster.")
        .arg(info.isDir() ? QStringLiteral("folder") : QStringLiteral("file"), QDir::toNativeSeparators(path)));
    setSourceMode(info.isDir() ? QStringLiteral("folder") : QStringLiteral("file"));
    Q_EMIT manifestsChanged();

    if (m_autoStart) {
        QMetaObject::invokeMethod(this, &COpyController::startCopy, Qt::QueuedConnection);
    }
    return true;
}

bool COpyController::loadManifest(const QString &pathOrUrl)
{
    const QString path = QFileInfo(localPathFromInput(pathOrUrl)).absoluteFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("Could not open manifest %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        appendLog(QStringLiteral("Manifest %1 is not valid JSON: %2").arg(QDir::toNativeSeparators(path), parseError.errorString()));
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != kManifestFormat
        && (hasClusters(root) || root.contains(QStringLiteral("destinations")))) {
        // A cluster file opened or dropped like a manifest.
        return importPresets(path);
    }
    if (root.value(QStringLiteral("format")).toString() != kManifestFormat) {
        appendLog(QStringLiteral("%1 is not a C-Opy manifest (expected \"format\": \"%2\").").arg(QDir::toNativeSeparators(path), kManifestFormat));
        return false;
    }

    for (const Manifest &existing : std::as_const(m_manifests)) {
        if (!existing.queued && existing.kind == SourceKind::Manifest && QFileInfo(existing.path) == QFileInfo(path)) {
            appendLog(QStringLiteral("Manifest %1 is already loaded.").arg(QDir::toNativeSeparators(path)));
            return true;
        }
    }

    Manifest manifest;
    manifest.id = ++m_nextManifestId;
    manifest.path = path;
    manifest.jobName = root.value(QStringLiteral("jobName")).toString(QFileInfo(path).completeBaseName());
    manifest.generator = root.value(QStringLiteral("generator")).toString();
    manifest.created = root.value(QStringLiteral("created")).toString();
    manifest.outputRoot = root.value(QStringLiteral("outputRoot")).toString();

    const QDir manifestDir = QFileInfo(path).absoluteDir();
    for (const QJsonValue &value : root.value(QStringLiteral("outputs")).toArray()) {
        const QJsonObject object = value.toObject();
        ManifestOutput output;
        output.identifier = object.value(QStringLiteral("identifier")).toString().trimmed();
        if (output.identifier.isEmpty() || output.identifier == kDefaultIdentifier) {
            continue;
        }
        QString source = object.value(QStringLiteral("sourceDirectory")).toString(output.identifier);
        if (QDir::isRelativePath(source)) {
            source = manifestDir.filePath(source);
        } else if (!QFileInfo(source).isDir() && QFileInfo(manifestDir.filePath(output.identifier)).isDir()) {
            // The output folder was moved together with its manifest.
            source = manifestDir.filePath(output.identifier);
        }
        output.sourceDirectory = QDir::cleanPath(source);
        output.filePattern = object.value(QStringLiteral("filePattern")).toString(QStringLiteral("*"));
        manifest.outputs << output;
    }

    if (manifest.outputs.isEmpty()) {
        appendLog(QStringLiteral("Manifest %1 contains no outputs.").arg(QDir::toNativeSeparators(path)));
        return false;
    }

    m_manifests << manifest;
    appendLog(QStringLiteral("Loaded '%1' with %2 output(s) from %3.")
        .arg(manifest.jobName)
        .arg(manifest.outputs.size())
        .arg(QDir::toNativeSeparators(path)));
    setSourceMode(QStringLiteral("manifest"));
    Q_EMIT manifestsChanged();
    Q_EMIT destinationsChanged();

    if (m_autoStart) {
        // Queued so that several manifests given at once are loaded before copying starts.
        QMetaObject::invokeMethod(this, &COpyController::startCopy, Qt::QueuedConnection);
    }
    return true;
}

void COpyController::removeManifest(int index)
{
    if (index < 0 || index >= m_manifests.size()) {
        return;
    }
    const int manifestId = m_manifests.at(index).id;
    if (manifestBusy(manifestId)) {
        appendLog(QStringLiteral("'%1' is still copying; stop it before removing.").arg(m_manifests.at(index).jobName));
        return;
    }
    m_tasks.removeIf([manifestId](const CopyTask &task) { return task.manifestId == manifestId; });
    const QString keyPrefix = QString::number(manifestId) + QLatin1Char('\n');
    m_disabledCopies.removeIf([&keyPrefix](const QString &key) { return key.startsWith(keyPrefix); });
    m_manifests.removeAt(index);
    Q_EMIT manifestsChanged();
    Q_EMIT destinationsChanged();
    scheduleTasksChanged();
}

void COpyController::setPlannedCopyEnabled(const QString &key, bool enabled)
{
    if (key.isEmpty() || m_disabledCopies.contains(key) != enabled) {
        return;
    }
    if (enabled) {
        m_disabledCopies.remove(key);
    } else {
        m_disabledCopies.insert(key);
    }
    Q_EMIT plannedCopiesChanged();
}

void COpyController::addDestination(const QString &identifier, const QString &path)
{
    if (identifier.isEmpty()) {
        return;
    }
    m_destinations[identifier] << path.trimmed();
    destinationsEdited();
}

void COpyController::setDestination(const QString &identifier, int index, const QString &path)
{
    auto it = m_destinations.find(identifier);
    if (it == m_destinations.end() || index < 0 || index >= it->size() || it->at(index) == path.trimmed()) {
        return;
    }
    (*it)[index] = path.trimmed();
    destinationsEdited();
}

void COpyController::removeDestination(const QString &identifier, int index)
{
    auto it = m_destinations.find(identifier);
    if (it == m_destinations.end() || index < 0 || index >= it->size()) {
        return;
    }
    it->removeAt(index);
    if (it->isEmpty()) {
        m_destinations.erase(it);
    }
    destinationsEdited();
}

QVariantList COpyController::storedAccounts() const
{
    QVariantList result;
    for (const auto &account : m_storedAccounts) {
        result << QVariantMap {
            { QStringLiteral("name"), account.first },
            { QStringLiteral("userName"), account.second },
        };
    }
    return result;
}

QStringList COpyController::storedAccountNames() const
{
    QStringList names;
    for (const auto &account : m_storedAccounts) {
        names << account.first;
    }
    return names;
}

bool COpyController::accountsSupported() const
{
    return COpyCredentials::supported();
}

QString COpyController::defaultAccount() const
{
    return COpySettings::defaultAccount();
}

void COpyController::setDefaultAccount(const QString &accountName)
{
    if (accountName.trimmed() == defaultAccount()) {
        return;
    }
    COpySettings::setDefaultAccount(accountName.trimmed());
    COpySettings::self()->save();
    Q_EMIT accountSettingsChanged();
    Q_EMIT destinationsChanged();
}

QVariantMap COpyController::clusterAccounts() const
{
    QVariantMap result;
    for (auto it = m_clusterAccounts.cbegin(); it != m_clusterAccounts.cend(); ++it) {
        result.insert(it.key(), it.value());
    }
    return result;
}

void COpyController::setClusterAccount(const QString &clusterName, const QString &accountName)
{
    if (clusterName.isEmpty() || m_clusterAccounts.value(clusterName) == accountName.trimmed()) {
        return;
    }
    KConfigGroup group = COpySettings::self()->sharedConfig()->group(kClusterAccountsGroup);
    if (accountName.trimmed().isEmpty()) {
        m_clusterAccounts.remove(clusterName);
        group.deleteGroup(clusterName);
    } else {
        m_clusterAccounts.insert(clusterName, accountName.trimmed());
        group.group(clusterName).writeEntry(kAccountKey, accountName.trimmed());
    }
    COpySettings::self()->save();
    Q_EMIT accountSettingsChanged();
    Q_EMIT destinationsChanged();
}

void COpyController::setOutputAccount(const QString &identifier, const QString &accountName)
{
    if (identifier.isEmpty() || m_accounts.value(identifier) == accountName.trimmed()) {
        return;
    }
    if (accountName.trimmed().isEmpty()) {
        m_accounts.remove(identifier);
    } else {
        m_accounts.insert(identifier, accountName.trimmed());
    }
    destinationsEdited();
}

void COpyController::setOutputNode(const QString &identifier, const QString &nodeName)
{
    if (identifier.isEmpty() || m_outputNodes.value(identifier) == nodeName.trimmed()) {
        return;
    }
    if (nodeName.trimmed().isEmpty()) {
        m_outputNodes.remove(identifier);
    } else {
        m_outputNodes.insert(identifier, nodeName.trimmed());
    }
    destinationsEdited();
}

bool COpyController::setNodeAddress(const QString &name, const QString &ipAddress)
{
    auto node = std::find_if(m_nodes.begin(), m_nodes.end(), [&name](const Node &node) { return node.name == name; });
    if (node == m_nodes.end()) {
        return false;
    }
    const QString address = ipAddress.trimmed();
    if (node->ipAddress == address) {
        return true;
    }

    QString error;
    QJsonObject object = readJsonObject(node->filePath, &error);
    if (error.isEmpty()) {
        object.insert(QStringLiteral("ip"), address);
        writeJsonObject(node->filePath, object, &error);
    }
    if (!error.isEmpty()) {
        appendLog(QStringLiteral("Could not update node '%1' in %2: %3").arg(node->name, QDir::toNativeSeparators(node->filePath), error));
        Q_EMIT nodesChanged();
        return false;
    }
    appendLog(QStringLiteral("Node '%1': %2 → %3 (%4).")
        .arg(node->name, node->ipAddress.isEmpty() ? QStringLiteral("no address") : node->ipAddress, address,
            QDir::toNativeSeparators(node->filePath)));
    node->ipAddress = address;
    Q_EMIT nodesChanged();
    Q_EMIT destinationsChanged();
    return true;
}

bool COpyController::addNode(const QString &name, const QString &ipAddress, const QString &description)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    if (findNode(trimmed)) {
        appendLog(QStringLiteral("A node named '%1' already exists.").arg(trimmed));
        return false;
    }
    static const QRegularExpression invalidFileNameCharacters(QStringLiteral("[<>:\"/\\\\|?*\\s]"));
    QString fileName = trimmed;
    fileName.replace(invalidFileNameCharacters, QStringLiteral("_"));
    const QDir folder(nodeFolder());
    const QString path = folder.filePath(fileName + QStringLiteral(".json"));
    if (QFileInfo::exists(path)) {
        appendLog(QStringLiteral("Could not add node '%1': %2 already exists.").arg(trimmed, QDir::toNativeSeparators(path)));
        return false;
    }

    QJsonObject object {
        { QStringLiteral("name"), trimmed },
        { QStringLiteral("ip"), ipAddress.trimmed() },
    };
    if (!description.trimmed().isEmpty()) {
        object.insert(QStringLiteral("description"), description.trimmed());
    }
    QString error;
    if (!QDir().mkpath(folder.absolutePath())) {
        error = QStringLiteral("cannot create the node folder");
    } else {
        writeJsonObject(path, object, &error);
    }
    if (!error.isEmpty()) {
        appendLog(QStringLiteral("Could not add node '%1' as %2: %3").arg(trimmed, QDir::toNativeSeparators(path), error));
        return false;
    }
    appendLog(QStringLiteral("Added node '%1' (%2) as %3.").arg(trimmed, ipAddress.trimmed(), QDir::toNativeSeparators(path)));
    loadNodes();
    return true;
}

void COpyController::reloadLibrary()
{
    loadLibraryPresets();
    loadNodes();
    appendLog(QStringLiteral("Reloaded clusters from %1 and nodes from %2.").arg(presetFolder(), nodeFolder()));
}

void COpyController::openPresetFolder() const
{
    QDir().mkpath(presetFolder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(presetFolder()));
}

void COpyController::openNodeFolder() const
{
    QDir().mkpath(nodeFolder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(nodeFolder()));
}

bool COpyController::storeAccount(const QString &name, const QString &userName, const QString &password)
{
    QString error;
    if (!COpyCredentials::store(name, userName, password, &error)) {
        appendLog(QStringLiteral("Could not store account '%1': %2").arg(name.trimmed(), error));
        return false;
    }
    appendLog(QStringLiteral("Stored account '%1' (%2) in Windows Credential Manager.").arg(name.trimmed(), userName.trimmed()));
    refreshAccounts();
    return true;
}

void COpyController::removeAccount(const QString &name)
{
    QString error;
    if (!COpyCredentials::remove(name, &error)) {
        appendLog(QStringLiteral("Could not remove account '%1': %2").arg(name, error));
    } else {
        appendLog(QStringLiteral("Removed account '%1' from Windows Credential Manager.").arg(name));
    }
    refreshAccounts();
}

void COpyController::refreshAccounts()
{
    m_storedAccounts.clear();
    for (const COpyCredentials::Account &account : COpyCredentials::accounts()) {
        m_storedAccounts << qMakePair(account.name, account.userName);
    }
    Q_EMIT storedAccountsChanged();
    Q_EMIT destinationsChanged();
}

void COpyController::applyPreset(const QString &name)
{
    const int index = presetIndex(name);
    if (index < 0) {
        return;
    }
    const Preset &preset = m_presets.at(index);
    m_currentPreset = preset.name;
    m_destinations = preset.destinations;
    m_accounts = preset.accounts;
    m_outputNodes = preset.nodes;
    m_driveShares = preset.driveShares;
    m_appliedSignature = mappingSignature(m_destinations, m_accounts, m_outputNodes, m_driveShares);
    Q_EMIT currentPresetChanged();
    Q_EMIT destinationsChanged();
    Q_EMIT presetModifiedChanged();
    saveSettings();
    appendLog(QStringLiteral("Applied cluster '%1'.").arg(preset.name));
}

void COpyController::savePreset(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const int index = presetIndex(trimmed);
    if (index >= 0 && !m_presets.at(index).filePath.isEmpty()) {
        appendLog(QStringLiteral("'%1' is a library cluster from %2. Save it under another name, or edit the file.")
            .arg(trimmed, QDir::toNativeSeparators(m_presets.at(index).filePath)));
        return;
    }
    Preset preset;
    preset.name = trimmed;
    preset.description = index >= 0 ? m_presets.at(index).description : QString();
    preset.destinations = normalizedDestinations(m_destinations);
    preset.accounts = m_accounts;
    preset.nodes = m_outputNodes;
    preset.driveShares = m_driveShares;
    m_appliedSignature = mappingSignature(m_destinations, m_accounts, m_outputNodes, m_driveShares);
    if (index >= 0) {
        m_presets[index] = preset;
    } else {
        m_presets << preset;
        Q_EMIT presetsChanged();
    }
    if (m_currentPreset != trimmed) {
        m_currentPreset = trimmed;
        Q_EMIT currentPresetChanged();
    }
    Q_EMIT presetModifiedChanged();
    saveSettings();
    appendLog(QStringLiteral("Saved cluster '%1'.").arg(trimmed));
}

void COpyController::deletePreset(const QString &name)
{
    const int index = presetIndex(name);
    if (index < 0) {
        return;
    }
    if (!m_presets.at(index).filePath.isEmpty()) {
        appendLog(QStringLiteral("'%1' is a library cluster; remove %2 to delete it.")
            .arg(name, QDir::toNativeSeparators(m_presets.at(index).filePath)));
        return;
    }
    m_presets.removeAt(index);
    Q_EMIT presetsChanged();
    setClusterAccount(name, QString());
    if (m_currentPreset == name) {
        m_currentPreset.clear();
        Q_EMIT currentPresetChanged();
    }
    Q_EMIT presetModifiedChanged();
    saveSettings();
    appendLog(QStringLiteral("Deleted cluster '%1'.").arg(name));
}

bool COpyController::importPresets(const QString &pathOrUrl)
{
    const QString path = QFileInfo(localPathFromInput(pathOrUrl)).absoluteFilePath();
    QString error;
    const QJsonObject root = readJsonObject(path, &error);
    if (!error.isEmpty()) {
        appendLog(QStringLiteral("Could not read cluster file %1: %2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }

    QStringList imported;
    for (const Preset &preset : presetsFromJson(root)) {
        const int index = presetIndex(preset.name);
        if (index >= 0 && !m_presets.at(index).filePath.isEmpty()) {
            appendLog(QStringLiteral("Skipped cluster '%1': a library cluster has that name.").arg(preset.name));
            continue;
        }
        if (index >= 0) {
            m_presets[index] = preset;
        } else {
            m_presets << preset;
        }
        imported << preset.name;
    }

    if (imported.isEmpty()) {
        appendLog(QStringLiteral("No clusters found in %1.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    Q_EMIT presetsChanged();
    saveSettings();
    appendLog(QStringLiteral("Imported cluster(s) %1 from %2.").arg(imported.join(QStringLiteral(", ")), QDir::toNativeSeparators(path)));
    applyPreset(imported.first());
    return true;
}

QList<COpyController::PlannedCopy> COpyController::planCopies() const
{
    QList<PlannedCopy> plan;
    const bool skipLocal = skipLocalNode() && !localNode().isEmpty();
    for (const Manifest &manifest : m_manifests) {
        if (manifest.queued) {
            continue;
        }

        // Every entry of this source starts from the same fields.
        const auto entry = [&manifest](const ManifestOutput &output) {
            PlannedCopy copy;
            copy.manifestId = manifest.id;
            copy.jobName = manifest.jobName;
            copy.identifier = output.identifier;
            copy.source = output.sourceDirectory;
            copy.filePattern = output.filePattern;
            return copy;
        };
        // Opened files are copied; folders and manifest outputs (which C-Slice writes into
        // folders of their own) are mirrored. The profile follows single file or folder.
        const bool mirror = manifest.kind != SourceKind::File;
        const auto finish = [this, mirror](PlannedCopy &copy, bool singleFile) {
            copy.profileName = robocopyProfileFor(singleFile).name;
            copy.mirror = mirror;
            if (!copy.skipped.isEmpty()) {
                return;
            }
            if (copy.mirror && isDriveOrShareRoot(copy.destination)) {
                copy.skipped = QStringLiteral("mirroring into a drive or share root would delete everything else there; add a subfolder such as {folder}");
                return;
            }
            const QString source = comparisonPath(copy.source);
            const QString destination = comparisonPath(copy.destination);
            if (copy.mirror && isDriveOrShareRoot(destination)) {
                copy.skipped = QStringLiteral("mirroring into a drive or share root would delete everything else there; add a subfolder such as {folder}");
            } else if (source == destination) {
                copy.skipped = QStringLiteral("source and destination are the same folder");
            } else if (copy.mirror && (isSameOrChildPath(destination, source) || isSameOrChildPath(source, destination))) {
                copy.skipped = QStringLiteral("source and destination folders overlap; choose a destination outside the source and its parents");
            }
        };

        if (manifest.kind != SourceKind::Manifest) {
            // A file or folder goes to every node of the cluster: to the same path through the
            // share of its drive (D:\Media\Show -> \\{ip}\d$\Media\Show), or else to the default
            // rule's destinations.
            const ManifestOutput &output = manifest.outputs.constFirst();
            const bool singleFile = manifest.kind == SourceKind::File;
            const bool usesSamePath = samePath();
            const QString drive = normalizedDrive(output.sourceDirectory);
            // The source folder below the drive root, e.g. "Media\Show"; appended after the
            // share is expanded, so braces in folder names are never read as tokens.
            const QString relativePath = QDir::toNativeSeparators(output.sourceDirectory).mid(3);
            const QStringList targets = usesSamePath ? QStringList { m_driveShares.value(drive) }
                                                     : nonEmptyEntries(m_destinations.value(kDefaultIdentifier));
            const QStringList nodes = clusterNodes();
            QString problem;
            if (nodes.isEmpty()) {
                problem = QStringLiteral("no cluster with nodes is selected");
            } else if (usesSamePath && drive.isEmpty()) {
                problem = QStringLiteral("not on a drive letter, so there is no same path on the nodes");
            } else if (usesSamePath && targets.constFirst().isEmpty()) {
                problem = QStringLiteral("drive %1 has no share in this cluster").arg(drive);
            } else if (targets.isEmpty()) {
                problem = QStringLiteral("the cluster has no default destination");
            }
            if (!problem.isEmpty()) {
                PlannedCopy copy = entry(output);
                copy.skipped = problem;
                finish(copy, singleFile);
                plan << copy;
                continue;
            }
            const QString folderName = QFileInfo(output.sourceDirectory).fileName();
            for (const QString &node : nodes) {
                for (const QString &target : targets) {
                    PlannedCopy copy = entry(output);
                    copy.node = node;
                    copy.accountName = accountForNode(node);
                    if (skipLocal && isLocalNode(node)) {
                        copy.skipped = QStringLiteral("this computer");
                        copy.local = true;
                    } else {
                        QString error;
                        copy.destination = expandDestination(target, folderName, manifest.jobName, output.identifier, node, nullptr, &error);
                        copy.skipped = error;
                        if (usesSamePath && error.isEmpty() && !relativePath.isEmpty()) {
                            copy.destination = QDir::toNativeSeparators(copy.destination);
                            while (copy.destination.endsWith(QLatin1Char('\\'))) {
                                copy.destination.chop(1);
                            }
                            copy.destination += QLatin1Char('\\') + relativePath;
                        }
                    }
                    finish(copy, singleFile);
                    plan << copy;
                }
            }
            continue;
        }

        const QString folderName = QFileInfo(manifest.path).absoluteDir().dirName();
        for (const ManifestOutput &output : manifest.outputs) {
            const bool singleFile = !output.filePattern.contains(QLatin1Char('*')) && !output.filePattern.contains(QLatin1Char('?'));
            QStringList targets = nonEmptyEntries(m_destinations.value(output.identifier));
            if (targets.isEmpty()) {
                targets = nonEmptyEntries(m_destinations.value(kManifestIdentifier));
            }
            if (targets.isEmpty()) {
                targets = nonEmptyEntries(m_destinations.value(kDefaultIdentifier));
            }
            if (targets.isEmpty()) {
                PlannedCopy copy = entry(output);
                copy.skipped = QStringLiteral("no destination");
                finish(copy, singleFile);
                plan << copy;
                continue;
            }
            for (const QString &target : std::as_const(targets)) {
                PlannedCopy copy = entry(output);
                copy.accountName = m_accounts.value(output.identifier, defaultRuleAccount());
                QString error;
                QStringList usedNodes;
                copy.destination = expandDestination(target, folderName, manifest.jobName, output.identifier, QString(), &usedNodes, &error);
                copy.node = usedNodes.isEmpty() ? QString() : usedNodes.constFirst();
                copy.skipped = error;
                const bool local = std::any_of(usedNodes.cbegin(), usedNodes.cend(), [this](const QString &node) {
                    return isLocalNode(node);
                });
                if (copy.skipped.isEmpty() && skipLocal && local) {
                    copy.skipped = QStringLiteral("this computer");
                    copy.local = true;
                }
                finish(copy, singleFile);
                plan << copy;
            }
        }
    }
    QSet<QStringList> active;
    for (const CopyTask &task : m_tasks) {
        if (task.status == TaskStatus::Pending || task.process) {
            active.insert(copyKey(task.source, task.destination, task.filePattern, task.mirror));
        }
    }
    QSet<QStringList> planned;
    for (PlannedCopy &copy : plan) {
        if (!copy.skipped.isEmpty()) {
            continue;
        }
        if (m_disabledCopies.contains(plannedCopyKey(copy))) {
            copy.skipped = QStringLiteral("disabled");
            copy.disabled = true;
            continue;
        }
        const QStringList key = copyKey(copy.source, copy.destination, copy.filePattern, copy.mirror);
        if (active.contains(key)) {
            copy.skipped = QStringLiteral("the same copy is already queued or running");
        } else if (planned.contains(key)) {
            copy.skipped = QStringLiteral("the same source and destination are already planned");
        } else {
            planned.insert(key);
        }
    }
    return plan;
}

QString COpyController::plannedCopyKey(const PlannedCopy &copy)
{
    // Starts with the source id, so removeManifest() can drop the keys of a source.
    return QStringList { QString::number(copy.manifestId), copy.identifier, copy.node, comparisonPath(copy.destination) }
        .join(QLatin1Char('\n'));
}

void COpyController::startCopy()
{
    if (std::any_of(m_targets.cbegin(), m_targets.cend(), [](const CheckedTarget &target) {
            return target.action == QStringLiteral("deleting");
        })) {
        appendLog(QStringLiteral("Deleting is still running; start the copy when it has finished."));
        return;
    }
    int created = 0;
    for (const PlannedCopy &copy : planCopies()) {
        if (!copy.skipped.isEmpty()) {
            appendLog(QStringLiteral("'%1': %2%3 skipped, %4.")
                    .arg(copy.jobName, copy.identifier,
                        copy.node.isEmpty() ? QString() : QStringLiteral(" → %1").arg(copy.node), copy.skipped));
            continue;
        }
        CopyTask task;
        task.id = ++m_nextTaskId;
        task.manifestId = copy.manifestId;
        task.jobName = copy.jobName;
        task.identifier = copy.identifier;
        task.source = copy.source;
        task.destination = copy.destination;
        task.filePattern = copy.filePattern;
        task.accountName = copy.accountName;
        task.mirror = copy.mirror;
        task.skipHidden = skipHiddenFiles();
        task.profileName = copy.profileName;
        task.profileArguments = findRobocopyProfile(copy.profileName)->arguments;
        task.multithreaded = usesMultithreading(profileArguments(task.profileArguments));
        m_tasks << task;
        ++created;
    }
    for (Manifest &manifest : m_manifests) {
        manifest.queued = true;
    }

    if (created > 0) {
        appendLog(QStringLiteral("Queued %1 copy task(s).").arg(created));
    } else if (!std::any_of(m_tasks.cbegin(), m_tasks.cend(), [](const CopyTask &task) { return task.status == TaskStatus::Pending; })) {
        appendLog(QStringLiteral("Nothing to copy. Choose a file, folder or manifest and a cluster first."));
    }
    Q_EMIT manifestsChanged();
    scheduleTasksChanged();
    pumpQueue();
}

void COpyController::stopCopy()
{
    int stopped = 0;
    for (CopyTask &task : m_tasks) {
        if (task.status == TaskStatus::Pending) {
            task.status = TaskStatus::Stopped;
            task.message = QStringLiteral("Stopped before start.");
            ++stopped;
        } else if (task.status == TaskStatus::Running) {
            // finishTask() keeps the Stopped status when the killed process reports back.
            task.status = TaskStatus::Stopped;
            task.message = QStringLiteral("Stopped while copying.");
            if (task.process) {
                task.process->kill();
            }
            ++stopped;
        }
    }
    if (stopped > 0) {
        appendLog(QStringLiteral("Stopped %1 copy task(s).").arg(stopped));
    }
    releaseFinishedManifests();
    Q_EMIT manifestsChanged();
    scheduleTasksChanged();
    updateRunning();
}

void COpyController::retryFailed()
{
    int retried = 0;
    for (CopyTask &task : m_tasks) {
        if ((task.status == TaskStatus::Failed || task.status == TaskStatus::Stopped) && !task.process) {
            task.status = TaskStatus::Pending;
            task.exitCode = -1;
            task.filesStarted = 0;
            task.fileProgress = 0.0;
            task.message.clear();
            for (Manifest &manifest : m_manifests) {
                if (manifest.id == task.manifestId) {
                    manifest.queued = true;
                }
            }
            ++retried;
        }
    }
    if (retried > 0) {
        appendLog(QStringLiteral("Retrying %1 copy task(s).").arg(retried));
    }
    Q_EMIT manifestsChanged();
    scheduleTasksChanged();
    pumpQueue();
}

void COpyController::clearFinished()
{
    m_tasks.removeIf([](const CopyTask &task) {
        return task.status != TaskStatus::Pending && task.status != TaskStatus::Running && !task.process;
    });
    // Manifests that were copied and no longer have tasks are done.
    m_manifests.removeIf([this](const Manifest &manifest) {
        return manifest.queued && std::none_of(m_tasks.cbegin(), m_tasks.cend(), [&manifest](const CopyTask &task) {
            return task.manifestId == manifest.id;
        });
    });
    Q_EMIT manifestsChanged();
    Q_EMIT destinationsChanged();
    scheduleTasksChanged();
}

QVariantList COpyController::targets() const
{
    QVariantList result;
    for (const CheckedTarget &target : m_targets) {
        QString state = target.action;
        if (state.isEmpty()) {
            state = !target.inspection.reachable ? QStringLiteral("unreachable")
                : target.inspection.exists       ? QStringLiteral("present")
                                                 : QStringLiteral("missing");
        }
        result << QVariantMap {
            { QStringLiteral("id"), target.id },
            { QStringLiteral("source"), target.source },
            { QStringLiteral("jobName"), target.jobName },
            { QStringLiteral("identifier"), target.identifier },
            { QStringLiteral("node"), target.node },
            { QStringLiteral("path"), targetPath(target.target) },
            { QStringLiteral("wholeFolder"), target.target.pattern == QStringLiteral("*") },
            { QStringLiteral("accountName"), target.target.accountName },
            { QStringLiteral("state"), state },
            { QStringLiteral("fileCount"), target.inspection.fileCount },
            { QStringLiteral("folderCount"), target.inspection.folderCount },
            { QStringLiteral("totalBytes"), target.inspection.totalBytes },
            { QStringLiteral("message"), target.message.isEmpty() ? target.inspection.error : target.message },
        };
    }
    return result;
}

QVariantList COpyController::checkedNodes() const
{
    QStringList order;
    QMap<QString, QVariantMap> byNode;
    for (const CheckedTarget &target : m_targets) {
        if (target.source || target.node.isEmpty()) {
            continue;
        }
        const QString key = target.node.toCaseFolded();
        if (!byNode.contains(key)) {
            order << key;
            const Node *node = findNode(target.node);
            byNode.insert(key, QVariantMap {
                { QStringLiteral("node"), target.node },
                { QStringLiteral("address"), node ? node->ipAddress : QString() },
                { QStringLiteral("state"), QStringLiteral("unreachable") },
                { QStringLiteral("message"), QString() },
            });
        }
        // A node is reachable when any of its destinations is, and checking while none is known.
        QVariantMap &entry = byNode[key];
        const QString state = entry.value(QStringLiteral("state")).toString();
        if (target.checked && target.inspection.reachable) {
            entry.insert(QStringLiteral("state"), QStringLiteral("reachable"));
            entry.insert(QStringLiteral("message"), QString());
        } else if (!target.checked && state != QStringLiteral("reachable")) {
            entry.insert(QStringLiteral("state"), QStringLiteral("checking"));
        } else if (state == QStringLiteral("unreachable") && entry.value(QStringLiteral("message")).toString().isEmpty()) {
            entry.insert(QStringLiteral("message"), target.inspection.error);
        }
    }
    QVariantList result;
    for (const QString &key : std::as_const(order)) {
        result << byNode.value(key);
    }
    return result;
}

bool COpyController::targetsBusy() const
{
    return std::any_of(m_targets.cbegin(), m_targets.cend(), [](const CheckedTarget &target) {
        return !target.action.isEmpty();
    });
}

bool COpyController::targetsStale() const
{
    if (m_targets.isEmpty()) {
        return false;
    }
    QSet<QString> checked;
    for (const CheckedTarget &target : m_targets) {
        checked.insert(targetKey(target.node, target.target));
    }
    for (const Manifest &manifest : m_manifests) {
        if (!manifest.queued && manifest.kind != SourceKind::Manifest && !checked.contains(targetKey(QString(), sourceTarget(manifest)))) {
            return true;
        }
    }
    const QList<PlannedCopy> plan = planCopies();
    return std::any_of(plan.cbegin(), plan.cend(), [&checked](const PlannedCopy &copy) {
        return copy.skipped.isEmpty() && !checked.contains(targetKey(copy.node, copyTarget(copy)));
    });
}

void COpyController::checkTargets()
{
    if (targetsBusy()) {
        appendLog(QStringLiteral("A check or delete is still running."));
        return;
    }
    m_targets.clear();
    QSet<QString> keys;
    const auto add = [this, &keys](CheckedTarget target) {
        const QString key = targetKey(target.node, target.target);
        if (!keys.contains(key)) {
            keys.insert(key);
            target.id = ++m_nextTargetId;
            m_targets << target;
        }
    };
    for (const Manifest &manifest : std::as_const(m_manifests)) {
        if (manifest.queued || manifest.kind == SourceKind::Manifest) {
            continue;
        }
        CheckedTarget target;
        target.manifestId = manifest.id;
        target.source = true;
        target.jobName = manifest.jobName;
        target.identifier = manifest.outputs.constFirst().identifier;
        target.target = sourceTarget(manifest);
        add(target);
    }
    for (const PlannedCopy &copy : planCopies()) {
        if (!copy.skipped.isEmpty()) {
            continue;
        }
        CheckedTarget target;
        target.manifestId = copy.manifestId;
        target.jobName = copy.jobName;
        target.identifier = copy.identifier;
        target.node = copy.node;
        target.target = copyTarget(copy);
        add(target);
    }
    if (m_targets.isEmpty()) {
        appendLog(QStringLiteral("Nothing to check. Choose a file, folder or manifest and a cluster first."));
    } else {
        appendLog(QStringLiteral("Checking %1 destination(s) and source(s)…").arg(m_targets.size()));
    }
    for (CheckedTarget &target : m_targets) {
        runTargetJob(target, false);
    }
    Q_EMIT targetsChanged();
}

void COpyController::deleteTargets(const QVariantList &ids)
{
    if (m_running) {
        appendLog(QStringLiteral("Copies are running; stop them or wait before deleting."));
        return;
    }
    int started = 0;
    for (const QVariant &id : ids) {
        const auto it = std::find_if(m_targets.begin(), m_targets.end(), [&id](const CheckedTarget &target) {
            return target.id == id.toInt();
        });
        if (it == m_targets.end() || !it->action.isEmpty() || !it->inspection.exists) {
            continue;
        }
        CheckedTarget &target = *it;
        const QString where = targetLocation(target);
        // The same rule as for mirroring: a drive or share root is never emptied.
        if (target.target.pattern == QStringLiteral("*") && isDriveOrShareRoot(target.target.folder)) {
            target.message = QStringLiteral("a drive or share root is never deleted");
            appendLog(QStringLiteral("Not deleting %1 on %2: %3.").arg(targetPath(target.target), where, target.message));
            continue;
        }
        appendLog(QStringLiteral("Deleting %1 on %2%3…")
                .arg(targetPath(target.target), where,
                    target.target.accountName.isEmpty() ? QString() : QStringLiteral(" as account '%1'").arg(target.target.accountName)));
        runTargetJob(target, true);
        ++started;
    }
    if (started == 0 && !ids.isEmpty()) {
        appendLog(QStringLiteral("Nothing to delete."));
    }
    Q_EMIT targetsChanged();
}

COpyTargets::Target COpyController::copyTarget(const PlannedCopy &copy)
{
    COpyTargets::Target target;
    target.folder = copy.destination;
    target.accountName = copy.accountName;
    const bool wildcard = copy.filePattern.contains(QLatin1Char('*')) || copy.filePattern.contains(QLatin1Char('?'));
    if (!copy.mirror || !wildcard) {
        // A single file (an opened file or a movie output) in the destination folder.
        target.pattern = copy.filePattern;
    } else if (copy.filePattern != QStringLiteral("*")) {
        // Mirrored with a pattern: only the matching files belong to the copy.
        target.pattern = copy.filePattern;
        target.recursive = true;
    }
    return target;
}

COpyTargets::Target COpyController::sourceTarget(const Manifest &manifest)
{
    COpyTargets::Target target;
    if (manifest.kind == SourceKind::File) {
        const QFileInfo info(manifest.path);
        target.folder = QDir::cleanPath(info.absolutePath());
        target.pattern = info.fileName();
    } else {
        target.folder = manifest.path;
    }
    return target;
}

QString COpyController::targetKey(const QString &node, const COpyTargets::Target &target)
{
    return QStringList { node.toCaseFolded(), comparisonPath(target.folder), target.pattern.toCaseFolded(),
        target.recursive ? QStringLiteral("recursive") : QString() }
        .join(QLatin1Char('\n'));
}

QString COpyController::targetPath(const COpyTargets::Target &target)
{
    QString path = QDir::toNativeSeparators(target.folder);
    if (target.pattern == QStringLiteral("*")) {
        return path;
    }
    if (!path.endsWith(QLatin1Char('\\'))) {
        path += QLatin1Char('\\');
    }
    // "…\*.png": the matching files in every subfolder.
    return path + (target.recursive ? QStringLiteral("…\\") : QString()) + target.pattern;
}

QString COpyController::targetLocation(const CheckedTarget &target)
{
    if (target.source) {
        return QStringLiteral("this computer");
    }
    return target.node.isEmpty() ? QStringLiteral("the destination") : target.node;
}

void COpyController::runTargetJob(CheckedTarget &target, bool remove)
{
    target.action = remove ? QStringLiteral("deleting") : QStringLiteral("checking");
    target.message.clear();
    const int targetId = target.id;
    const COpyTargets::Target spec = target.target;
    const std::shared_ptr<std::atomic_bool> cancelled = m_targetsCancelled;
    m_targetPool.start([this, targetId, spec, remove, cancelled]() {
        QString error;
        if (remove) {
            COpyTargets::remove(spec, *cancelled, &error);
        }
        // Checked again after deleting, so the list shows what is left.
        const COpyTargets::Inspection inspection = COpyTargets::inspect(spec, *cancelled);
        QMetaObject::invokeMethod(this, [this, targetId, remove, inspection, error]() {
            finishTargetJob(targetId, remove, inspection, error);
        }, Qt::QueuedConnection);
    });
}

void COpyController::finishTargetJob(int targetId, bool removed, const COpyTargets::Inspection &inspection, const QString &error)
{
    const auto it = std::find_if(m_targets.begin(), m_targets.end(), [targetId](const CheckedTarget &target) {
        return target.id == targetId;
    });
    if (it == m_targets.end()) {
        return;
    }
    CheckedTarget &target = *it;
    const bool wasPresent = target.inspection.exists;
    target.action.clear();
    target.checked = true;
    target.inspection = inspection;
    const QString where = targetLocation(target);
    const QString path = targetPath(target.target);
    bool sourceDeleted = false;
    if (removed) {
        if (!error.isEmpty()) {
            target.message = error;
            appendLog(QStringLiteral("Could not delete %1 on %2: %3").arg(path, where, error));
        } else if (wasPresent && !inspection.exists) {
            target.message = QStringLiteral("Deleted.");
            appendLog(QStringLiteral("Deleted %1 on %2.").arg(path, where));
            sourceDeleted = target.source;
        }
    } else if (!inspection.reachable) {
        appendLog(QStringLiteral("Cannot reach %1 on %2: %3").arg(path, where, inspection.error));
    } else if (inspection.exists) {
        appendLog(QStringLiteral("%1 on %2 already exists: %3 file(s), %4.")
                .arg(path, where).arg(inspection.fileCount).arg(formatDataSize(inspection.totalBytes)));
    }
    if (!removed && !targetsBusy()) {
        const auto present = std::count_if(m_targets.cbegin(), m_targets.cend(), [](const CheckedTarget &other) {
            return other.inspection.exists;
        });
        const auto unreachable = std::count_if(m_targets.cbegin(), m_targets.cend(), [](const CheckedTarget &other) {
            return !other.inspection.reachable;
        });
        appendLog(QStringLiteral("Check finished: %1 of %2 already exist, %3 cannot be reached.")
                .arg(present).arg(m_targets.size()).arg(unreachable));
    }
    Q_EMIT targetsChanged();
    // Nothing is left to copy from a deleted source.
    if (sourceDeleted) {
        const int manifestId = target.manifestId;
        const auto source = std::find_if(m_manifests.cbegin(), m_manifests.cend(), [manifestId](const Manifest &manifest) {
            return manifest.id == manifestId;
        });
        if (source != m_manifests.cend()) {
            removeManifest(static_cast<int>(source - m_manifests.cbegin()));
        }
    }
}

void COpyController::clearLog()
{
    m_logText.clear();
    Q_EMIT logTextChanged();
}

void COpyController::openSettingsFolder() const
{
    const QString folder = QFileInfo(settingsFilePath()).absolutePath();
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

QUrl COpyController::presetExamplesFolder() const
{
    // data/copy next to the installed executable, or in the source tree when run from a build folder.
    const QString applicationDir = QCoreApplication::applicationDirPath();
    for (const QString &relative : { QStringLiteral("data/copy"), QStringLiteral("../data/copy"),
             QStringLiteral("../../data/copy"), QStringLiteral("../../../data/copy") }) {
        const QString candidate = QDir::cleanPath(QDir(applicationDir).filePath(relative));
        if (QFileInfo(candidate).isDir()) {
            return QUrl::fromLocalFile(candidate);
        }
    }
    return QUrl::fromLocalFile(applicationDir);
}

QString COpyController::urlToLocalPath(const QUrl &url) const
{
    return QDir::toNativeSeparators(url.toLocalFile());
}

void COpyController::rememberManifestDialogFolder(const QUrl &url)
{
    const QString folder = localFolder(url, true);
    if (folder.isEmpty() || folder == COpySettings::lastManifestDialogLocation()) {
        return;
    }
    COpySettings::setLastManifestDialogLocation(folder);
    COpySettings::self()->save();
    Q_EMIT dialogFoldersChanged();
}

void COpyController::rememberSourceDialogFolder(const QUrl &url, bool isFile)
{
    // A chosen folder is copied as a whole, so the dialog reopens in its parent.
    const QString folder = isFile ? localFolder(url, true) : QDir::toNativeSeparators(QFileInfo(url.toLocalFile()).absolutePath());
    if (url.toLocalFile().isEmpty() || folder == COpySettings::lastSourceDialogLocation()) {
        return;
    }
    COpySettings::setLastSourceDialogLocation(folder);
    COpySettings::self()->save();
    Q_EMIT dialogFoldersChanged();
}

void COpyController::rememberPresetDialogFolder(const QUrl &url)
{
    const QString folder = localFolder(url, true);
    if (folder.isEmpty() || folder == COpySettings::lastPresetDialogLocation()) {
        return;
    }
    COpySettings::setLastPresetDialogLocation(folder);
    COpySettings::self()->save();
    Q_EMIT dialogFoldersChanged();
}

void COpyController::rememberDestinationDialogFolder(const QUrl &url)
{
    const QString folder = localFolder(url, false);
    if (folder.isEmpty() || folder == COpySettings::lastDestinationDialogLocation()) {
        return;
    }
    COpySettings::setLastDestinationDialogLocation(folder);
    COpySettings::self()->save();
    Q_EMIT dialogFoldersChanged();
}

void COpyController::loadSettings()
{
    m_maxParallel = std::clamp(COpySettings::maxParallel(), 1, kMaxParallelLimit);
    m_autoStart = COpySettings::autoStart();
    m_currentPreset = COpySettings::currentPreset();

    const KSharedConfig::Ptr config = COpySettings::self()->sharedConfig();
    const KConfigGroup presetsGroup = config->group(kPresetsGroup);
    QStringList names = presetsGroup.groupList();
    names.sort(Qt::CaseInsensitive);
    for (const QString &name : std::as_const(names)) {
        const KConfigGroup group = presetsGroup.group(name);
        Preset preset;
        preset.name = name;
        preset.description = group.readEntry(kDescriptionKey, QString());
        readOutputRules(group.group(kPresetOutputsGroup), preset.destinations, preset.accounts, preset.nodes);
        preset.driveShares = readDriveShares(group);
        m_presets << preset;
    }

    // The working mapping (including unsaved edits) survives restarts.
    readOutputRules(config->group(kCurrentOutputsGroup), m_destinations, m_accounts, m_outputNodes);
    m_driveShares = readDriveShares(config->group(kCurrentOutputsGroup));
    m_appliedSignature = config->group(kCurrentOutputsGroup).readEntry(kAppliedSignatureKey, QString());

    m_clusterAccounts.clear();
    const KConfigGroup clusterAccountsGroup = config->group(kClusterAccountsGroup);
    for (const QString &cluster : clusterAccountsGroup.groupList()) {
        const QString account = clusterAccountsGroup.group(cluster).readEntry(kAccountKey, QString()).trimmed();
        if (!account.isEmpty()) {
            m_clusterAccounts.insert(cluster, account);
        }
    }

    // Also forgets a current cluster that no longer exists.
    loadLibraryPresets();
}

QList<COpyController::Preset> COpyController::presetsFromJson(const QJsonObject &root)
{
    // Accepts a "clusters" array (or the older "presets"), or a single cluster object.
    QJsonArray presets = root.value(root.contains(QStringLiteral("clusters")) ? QStringLiteral("clusters") : QStringLiteral("presets")).toArray();
    if (presets.isEmpty() && root.contains(QStringLiteral("name"))) {
        presets.append(root);
    }

    QList<Preset> result;
    for (const QJsonValue &value : std::as_const(presets)) {
        const QJsonObject object = value.toObject();
        Preset preset;
        preset.name = object.value(QStringLiteral("name")).toString().trimmed();
        if (preset.name.isEmpty()) {
            continue;
        }
        preset.description = object.value(QStringLiteral("description")).toString().trimmed();
        preset.destinations = destinationsFromJson(object.value(QStringLiteral("destinations")).toObject());
        preset.destinations.remove(kManifestIdentifier);
        preset.destinations.insert(destinationsFromJson(QJsonObject {
            { kManifestIdentifier, object.value(QStringLiteral("manifestDestinations")) } }));
        preset.accounts = namesFromJson(object.value(QStringLiteral("accounts")).toObject());
        preset.nodes = namesFromJson(object.value(QStringLiteral("nodes")).toObject());
        preset.driveShares = driveSharesFromJson(object.value(QStringLiteral("driveShares")).toObject());
        result << preset;
    }
    return result;
}

void COpyController::loadLibraryPresets()
{
    // Saved clusters keep their names; a library cluster with the same name is skipped.
    m_presets.removeIf([](const Preset &preset) { return !preset.filePath.isEmpty(); });
    for (const QString &path : jsonFilesIn(presetFolder())) {
        QString error;
        QJsonObject root = readJsonObject(path, &error);
        if (!error.isEmpty()) {
            appendLog(QStringLiteral("Could not read cluster file %1: %2").arg(QDir::toNativeSeparators(path), error));
            continue;
        }
        if (!hasClusters(root) && !root.contains(QStringLiteral("name"))) {
            root.insert(QStringLiteral("name"), QFileInfo(path).completeBaseName());
        }
        for (Preset &preset : presetsFromJson(root)) {
            if (presetIndex(preset.name) >= 0) {
                appendLog(QStringLiteral("Skipped library cluster '%1' in %2: a saved cluster has that name; delete the saved one to use the file.")
                    .arg(preset.name, QDir::toNativeSeparators(path)));
                continue;
            }
            preset.filePath = path;
            m_presets << preset;
        }
    }
    std::stable_sort(m_presets.begin(), m_presets.end(), [](const Preset &a, const Preset &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    if (!m_currentPreset.isEmpty() && presetIndex(m_currentPreset) < 0) {
        m_currentPreset.clear();
    }
    syncCurrentLibraryPreset();
    Q_EMIT presetsChanged();
    Q_EMIT currentPresetChanged();
    Q_EMIT presetModifiedChanged();
}

void COpyController::syncCurrentLibraryPreset()
{
    const int index = presetIndex(m_currentPreset);
    if (index < 0 || m_presets.at(index).filePath.isEmpty()) {
        return;
    }
    const Preset &preset = m_presets.at(index);
    const QString fileSignature = mappingSignature(preset.destinations, preset.accounts, preset.nodes, preset.driveShares);
    if (fileSignature == m_appliedSignature) {
        return;
    }
    const QString workingSignature = mappingSignature(m_destinations, m_accounts, m_outputNodes, m_driveShares);
    if (workingSignature == fileSignature) {
        m_appliedSignature = fileSignature;
        saveSettings();
        return;
    }
    // The cluster file changed since it was applied. Without edits (or without a record of
    // the applied version, from older versions) the working mapping follows the file.
    if (!m_appliedSignature.isEmpty() && workingSignature != m_appliedSignature) {
        appendLog(QStringLiteral("Cluster '%1' changed in %2. Your edited destinations are kept; choose the cluster again to use the file.")
            .arg(preset.name, QDir::toNativeSeparators(preset.filePath)));
        return;
    }
    m_destinations = preset.destinations;
    m_accounts = preset.accounts;
    m_outputNodes = preset.nodes;
    m_driveShares = preset.driveShares;
    m_appliedSignature = fileSignature;
    Q_EMIT destinationsChanged();
    saveSettings();
    appendLog(QStringLiteral("Cluster '%1' changed in %2; applied the new version.").arg(preset.name, QDir::toNativeSeparators(preset.filePath)));
}

void COpyController::seedNodeFolder() const
{
    // On first start the machine's own node folder gets the bundled example nodes, which are
    // then edited for this network without touching the installation.
    const QString folder = defaultNodeFolder();
    const QStringList examples = jsonFilesIn(QDir(presetExamplesFolder().toLocalFile()).filePath(QStringLiteral("nodes")));
    if (examples.isEmpty() || QFileInfo(folder).isDir() || !QDir().mkpath(folder)) {
        return;
    }
    for (const QString &path : examples) {
        QFile::copy(path, QDir(folder).filePath(QFileInfo(path).fileName()));
    }
}

void COpyController::loadNodes()
{
    if (COpySettings::nodeFolder().trimmed().isEmpty()) {
        seedNodeFolder();
    }
    // The same files as C-Troll nodes: {"name": ..., "ip": ..., "description": ...}.
    m_nodes.clear();
    for (const QString &path : jsonFilesIn(nodeFolder())) {
        QString error;
        const QJsonObject object = readJsonObject(path, &error);
        if (!error.isEmpty()) {
            appendLog(QStringLiteral("Could not read node file %1: %2").arg(QDir::toNativeSeparators(path), error));
            continue;
        }
        Node node;
        node.name = object.value(QStringLiteral("name")).toString(QFileInfo(path).completeBaseName()).trimmed();
        node.ipAddress = object.value(QStringLiteral("ip")).toString().trimmed();
        node.description = object.value(QStringLiteral("description")).toString().trimmed();
        node.filePath = path;
        if (node.name.isEmpty()) {
            continue;
        }
        if (const Node *existing = findNode(node.name)) {
            appendLog(QStringLiteral("Skipped node '%1' in %2: already defined in %3.")
                .arg(node.name, QDir::toNativeSeparators(path), QDir::toNativeSeparators(existing->filePath)));
            continue;
        }
        m_nodes << node;
    }
    std::sort(m_nodes.begin(), m_nodes.end(), [](const Node &a, const Node &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    updateLocalNode();
    Q_EMIT nodesChanged();
    Q_EMIT destinationsChanged();
}

void COpyController::saveSettings() const
{
    COpySettings::setCurrentPreset(m_currentPreset);
    COpySettings::setMaxParallel(m_maxParallel);
    COpySettings::setAutoStart(m_autoStart);

    // Clusters and the working mapping are rewritten as a whole, so deleted clusters and
    // cleared outputs also disappear from the file.
    const KSharedConfig::Ptr config = COpySettings::self()->sharedConfig();
    KConfigGroup presetsGroup = config->group(kPresetsGroup);
    presetsGroup.deleteGroup();
    for (const Preset &preset : m_presets) {
        if (!preset.filePath.isEmpty()) {
            continue;
        }
        // Always written, so that a cluster without output rules keeps its group.
        KConfigGroup group = presetsGroup.group(preset.name);
        group.writeEntry(kDescriptionKey, preset.description);
        writeOutputRules(group.group(kPresetOutputsGroup), preset.destinations, preset.accounts, preset.nodes);
        writeDriveShares(group, preset.driveShares);
    }
    KConfigGroup currentGroup = config->group(kCurrentOutputsGroup);
    currentGroup.deleteGroup();
    writeOutputRules(currentGroup, m_destinations, m_accounts, m_outputNodes);
    writeDriveShares(currentGroup, m_driveShares);
    if (!m_appliedSignature.isEmpty()) {
        currentGroup.writeEntry(kAppliedSignatureKey, m_appliedSignature);
    }

    if (!COpySettings::self()->save()) {
        qWarning() << "Could not save C-Opy settings to" << settingsFilePath();
    }
}

void COpyController::destinationsEdited()
{
    Q_EMIT destinationsChanged();
    Q_EMIT presetModifiedChanged();
    saveSettings();
}

int COpyController::presetIndex(const QString &name) const
{
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets.at(i).name == name) {
            return i;
        }
    }
    return -1;
}

const COpyController::Node *COpyController::findNode(const QString &name) const
{
    // Node names are host names, so they match regardless of case.
    for (const Node &node : m_nodes) {
        if (node.name.compare(name, Qt::CaseInsensitive) == 0) {
            return &node;
        }
    }
    return nullptr;
}

QString COpyController::effectiveNodeName(const QString &identifier) const
{
    return m_outputNodes.value(identifier, m_outputNodes.value(kDefaultIdentifier));
}

QString COpyController::manifestStatus(const Manifest &manifest) const
{
    if (!manifest.queued) {
        return QStringLiteral("Ready");
    }
    bool any = false;
    bool active = false;
    bool problems = false;
    for (const CopyTask &task : m_tasks) {
        if (task.manifestId != manifest.id) {
            continue;
        }
        any = true;
        active |= task.status == TaskStatus::Pending || task.status == TaskStatus::Running;
        problems |= task.status == TaskStatus::Failed || task.status == TaskStatus::Stopped;
    }
    if (!any) {
        return QStringLiteral("No destinations");
    }
    if (active) {
        return QStringLiteral("Copying");
    }
    return problems ? QStringLiteral("Errors") : QStringLiteral("Copied");
}

bool COpyController::manifestBusy(int manifestId) const
{
    return std::any_of(m_tasks.cbegin(), m_tasks.cend(), [manifestId](const CopyTask &task) {
        return task.manifestId == manifestId && (task.status == TaskStatus::Running || task.process);
    });
}

QString COpyController::expandDestination(const QString &destination, const QString &folderName, const QString &jobName,
    const QString &identifier, const QString &nodeName, QStringList *usedNodes, QString *error) const
{
    // {ip:<node>} names a node directly, e.g. for a second copy on a backup machine.
    static const QRegularExpression namedNodeAddress(QStringLiteral("\\{ip:([^}]*)\\}"));
    const QString trimmed = destination.trimmed();
    QString result;
    qsizetype last = 0;
    for (auto it = namedNodeAddress.globalMatch(trimmed); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString nodeName = match.captured(1).trimmed();
        const Node *node = findNode(nodeName);
        if (!node || node->ipAddress.isEmpty()) {
            *error = node ? QStringLiteral("node '%1' has no IP address").arg(node->name)
                          : QStringLiteral("node '%1' is not in the node folder").arg(nodeName);
            return QString();
        }
        if (usedNodes) {
            *usedNodes << node->name;
        }
        result += trimmed.mid(last, match.capturedStart() - last) + node->ipAddress;
        last = match.capturedEnd();
    }
    result += trimmed.mid(last);

    // {ip} and {node} are the given node, or else the node assigned to the output or the default rule.
    if (result.contains(QStringLiteral("{ip}")) || result.contains(QStringLiteral("{node}"))) {
        const QString effectiveNode = nodeName.isEmpty() ? effectiveNodeName(identifier) : nodeName;
        const Node *node = findNode(effectiveNode);
        if (effectiveNode.isEmpty()) {
            *error = QStringLiteral("no node assigned for {ip}/{node}");
            return QString();
        }
        if (!node) {
            *error = QStringLiteral("node '%1' is not in the node folder").arg(effectiveNode);
            return QString();
        }
        if (usedNodes) {
            *usedNodes << node->name;
        }
        if (node->ipAddress.isEmpty() && result.contains(QStringLiteral("{ip}"))) {
            *error = QStringLiteral("node '%1' has no IP address").arg(node->name);
            return QString();
        }
        result.replace(QStringLiteral("{ip}"), node->ipAddress);
        result.replace(QStringLiteral("{node}"), node->name);
    }

    // {folder} is the output directory name for manifests (C-Slice writes the manifest into that
    // directory), the folder itself for an added folder, and the parent folder for an added file.
    result.replace(QStringLiteral("{folder}"), folderName);
    result.replace(QStringLiteral("{identifier}"), identifier);
    result.replace(QStringLiteral("{job}"), jobName);
    result.replace(QStringLiteral("{date}"), QDate::currentDate().toString(Qt::ISODate));
    return result;
}

bool COpyController::isLocalNode(const QString &name) const
{
    const QString local = localNode();
    return !local.isEmpty() && name.compare(local, Qt::CaseInsensitive) == 0;
}

QString COpyController::accountForNode(const QString &nodeName) const
{
    // The account of an output that is assigned this node, or else the default rule's account.
    for (auto it = m_outputNodes.cbegin(); it != m_outputNodes.cend(); ++it) {
        if (it.key() != kDefaultIdentifier && it.value().compare(nodeName, Qt::CaseInsensitive) == 0
            && m_accounts.contains(it.key())) {
            return m_accounts.value(it.key());
        }
    }
    return defaultRuleAccount();
}

QString COpyController::settingsAccount() const
{
    const QString clusterAccount = m_clusterAccounts.value(m_currentPreset);
    return clusterAccount.isEmpty() ? defaultAccount() : clusterAccount;
}

QString COpyController::defaultRuleAccount() const
{
    const QString account = m_accounts.value(kDefaultIdentifier);
    return account.isEmpty() ? settingsAccount() : account;
}

void COpyController::updateLocalNode()
{
    // A node is this computer when its name is the host name (or the first label of a fully
    // qualified one), or when its address is a host name or IP address of this computer.
    const QString host = hostName();
    const QString shortHost = host.section(QLatin1Char('.'), 0, 0);
    QList<QHostAddress> addresses;
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (!address.isLoopback()) {
            addresses << address;
        }
    }
    const auto matchesHost = [&host, &shortHost](const QString &name) {
        return !name.isEmpty()
            && (name.compare(host, Qt::CaseInsensitive) == 0
                || name.section(QLatin1Char('.'), 0, 0).compare(shortHost, Qt::CaseInsensitive) == 0);
    };

    QString detected;
    for (const Node &node : std::as_const(m_nodes)) {
        const QHostAddress address(node.ipAddress);
        if (matchesHost(node.name) || (address.isNull() ? matchesHost(node.ipAddress) : addresses.contains(address))) {
            detected = node.name;
            break;
        }
    }
    if (detected != m_detectedLocalNode) {
        m_detectedLocalNode = detected;
        Q_EMIT localNodeChanged();
    }
}

QList<COpyController::RobocopyProfile> COpyController::builtInRobocopyProfiles()
{
    return {
        { QStringLiteral("Large files"), QStringLiteral("/J /R:2 /W:5 /NDL"),
            QStringLiteral("Video files and other large files: unbuffered I/O (/J), progress per file, two retries five seconds apart.") },
        { QStringLiteral("Many small files"), QStringLiteral("/MT:16 /R:2 /W:5 /NDL /NP"),
            QStringLiteral("Image sequences and folders with many small files: 16 copy threads per robocopy (/MT:16), no per-file progress, two retries five seconds apart.") },
    };
}

const COpyController::RobocopyProfile *COpyController::findRobocopyProfile(const QString &name) const
{
    for (const RobocopyProfile &profile : m_robocopyProfiles) {
        if (profile.name == name) {
            return &profile;
        }
    }
    return nullptr;
}

void COpyController::loadRobocopyProfiles()
{
    // { "format": "c-opy-robocopy-profiles", "profiles": [ { "name", "arguments", "description" } ] }
    m_robocopyProfiles.clear();
    const QString path = robocopyProfilesFilePath();
    if (QFileInfo::exists(path)) {
        QString error;
        const QJsonObject root = readJsonObject(path, &error);
        if (!error.isEmpty()) {
            appendLog(QStringLiteral("Could not read robocopy profiles from %1: %2. Using the built-in profiles.").arg(path, error));
        }
        for (const QJsonValue &value : root.value(QStringLiteral("profiles")).toArray()) {
            const QJsonObject object = value.toObject();
            RobocopyProfile profile;
            profile.name = object.value(QStringLiteral("name")).toString().trimmed();
            profile.arguments = object.value(QStringLiteral("arguments")).toString().simplified();
            profile.description = object.value(QStringLiteral("description")).toString().trimmed();
            if (!profile.name.isEmpty() && !findRobocopyProfile(profile.name)) {
                m_robocopyProfiles << profile;
            }
        }
    }
    if (m_robocopyProfiles.isEmpty()) {
        m_robocopyProfiles = builtInRobocopyProfiles();
    }
    Q_EMIT robocopyProfilesChanged();
}

bool COpyController::saveRobocopyProfiles()
{
    QJsonArray profiles;
    for (const RobocopyProfile &profile : std::as_const(m_robocopyProfiles)) {
        profiles.append(QJsonObject {
            { QStringLiteral("name"), profile.name },
            { QStringLiteral("arguments"), profile.arguments },
            { QStringLiteral("description"), profile.description },
        });
    }
    const QJsonObject root {
        { QStringLiteral("format"), kRobocopyProfilesFormat },
        { QStringLiteral("version"), 1 },
        { QStringLiteral("profiles"), profiles },
    };
    const QString path = robocopyProfilesFilePath();
    QString error;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        error = QStringLiteral("cannot create the settings folder");
    } else {
        writeJsonObject(path, root, &error);
    }
    if (!error.isEmpty()) {
        appendLog(QStringLiteral("Could not save robocopy profiles to %1: %2").arg(path, error));
        return false;
    }
    return true;
}

void COpyController::applyStartupOptions()
{
    const QString cluster = startupCluster();
    if (!cluster.isEmpty()) {
        if (presetIndex(cluster) >= 0) {
            applyPreset(cluster);
        } else {
            appendLog(QStringLiteral("Startup cluster '%1' was not found; keeping '%2'.").arg(cluster, m_currentPreset));
        }
    }
}

const COpyController::RobocopyProfile &COpyController::robocopyProfileFor(bool singleFile) const
{
    const RobocopyProfile *profile = findRobocopyProfile(singleFile ? COpySettings::fileRobocopyProfile() : COpySettings::folderRobocopyProfile());
    return profile ? *profile : m_robocopyProfiles.constFirst();
}

COpyController::CopyTask *COpyController::taskById(int id)
{
    for (CopyTask &task : m_tasks) {
        if (task.id == id) {
            return &task;
        }
    }
    return nullptr;
}

void COpyController::pumpQueue()
{
    int runningCount = static_cast<int>(std::count_if(m_tasks.cbegin(), m_tasks.cend(), [](const CopyTask &task) {
        return task.process != nullptr;
    }));
    for (int i = 0; i < m_tasks.size() && runningCount < m_maxParallel; ++i) {
        if (m_tasks[i].status == TaskStatus::Pending) {
            startTask(m_tasks[i]);
            ++runningCount;
        }
    }
    updateRunning();
}

void COpyController::startTask(CopyTask &task)
{
    const int taskId = task.id;
    task.status = TaskStatus::Running;
    // Mirroring includes subfolders. Robocopy lists only the files it copies, so unchanged
    // files keep the progress below 100 % until the task is done.
    // Tasks that copy the same source to other destinations share one scan.
    task.scan.reset();
    for (const CopyTask &other : std::as_const(m_tasks)) {
        if (other.process && other.scan && other.source == task.source && other.filePattern == task.filePattern
            && other.mirror == task.mirror && other.skipHidden == task.skipHidden && other.multithreaded == task.multithreaded) {
            task.scan = other.scan;
            break;
        }
    }
    if (!task.scan) {
        task.scan = scanSource(task.source, task.filePattern, task.mirror, task.skipHidden, !task.multithreaded);
    }
    task.totalFiles = task.scan->fileCount;
    task.totalBytes = task.scan->totalBytes;
    task.filesStarted = 0;
    task.startedFiles.clear();
    task.fileProgress = 0.0;
    task.listedBytes = 0;
    task.writtenBytes = -1;
    task.doneBytes = 0;
    task.currentFile.clear();
    task.currentFileBytes = 0;
    task.passedBytes = 0;
    task.changedShare = 1.0;
    task.sharePassedBytes = 0;
    task.shareListedBytes = 0;
    task.run = m_run;
    task.rateSamples.clear();
    task.elapsedMs = 0;
    task.clock.start();
    task.outputBuffer.clear();
    // The progress bar and the file count show a running copy; message is left for errors.
    task.message.clear();

    QStringList arguments {
        robocopyFolderArgument(task.source),
        robocopyFolderArgument(task.destination),
        task.filePattern,
    };
    if (task.mirror) {
        arguments << QStringLiteral("/MIR");
    }
    if (task.skipHidden) {
        arguments << QStringLiteral("/XA:SH");
    }
    arguments << profileArguments(task.profileArguments);
    // Never expand linked trees outside the source. Full paths distinguish equal file
    // names in different subfolders when counting progress; byte totals stay exact.
    arguments << QStringLiteral("/XJ") << QStringLiteral("/BYTES") << QStringLiteral("/FP");

    auto *runner = new RobocopyRunner(this);
    task.process = runner;
    connect(runner, &RobocopyRunner::output, this, [this, taskId](const QString &text) {
        handleTaskOutput(taskId, text);
    });
    connect(runner, &RobocopyRunner::finished, this, [this, taskId](int exitCode, const QString &failure) {
        finishTask(taskId, exitCode, failure);
    });

    appendLog(QStringLiteral("[%1] %2 \u2192 %3 (%4)%5: robocopy %6")
        .arg(task.jobName, task.source, arguments.at(1), task.filePattern,
            task.accountName.isEmpty() ? QString() : QStringLiteral(" as account '%1'").arg(task.accountName),
            arguments.mid(2).join(QLatin1Char(' '))));
    scheduleTasksChanged();
    runner->start(arguments, task.accountName);
}

void COpyController::handleTaskOutput(int taskId, const QString &text)
{
    CopyTask *task = taskById(taskId);
    if (!task || !task->process) {
        return;
    }
    task->outputBuffer += text;
    const qint64 copiedBefore = taskCopiedBytes(*task);

    // Robocopy rewrites its per-file percentage with carriage returns.
    static const QRegularExpression lineBreak(QStringLiteral("[\r\n]"));
    QStringList lines = task->outputBuffer.split(lineBreak);
    task->outputBuffer = lines.takeLast();

    for (const QString &rawLine : std::as_const(lines)) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.endsWith(QLatin1Char('%'))) {
            bool ok = false;
            const double percent = line.chopped(1).trimmed().toDouble(&ok);
            if (ok) {
                task->fileProgress = percent / 100.0;
            }
            continue;
        }
        if (line.startsWith(QStringLiteral("New File")) || line.startsWith(QStringLiteral("Newer"))
            || line.startsWith(QStringLiteral("Older")) || line.startsWith(QStringLiteral("Changed"))) {
            // Only counted for the progress bar; file names are not shown.
            const QString file = rawLine.section(QLatin1Char('\t'), -1).trimmed();
            // /BYTES: the size column is in bytes.
            const qint64 size = std::max<qint64>(0, rawLine.section(QLatin1Char('\t'), -2, -2).trimmed().toLongLong());
            // A retry lists the same file again; only another file finishes the current one.
            if (file != task->currentFile) {
                task->doneBytes += task->currentFileBytes;
                task->currentFile = file;
                task->currentFileBytes = size;
            }
            task->fileProgress = 0.0;
            // A retry can list the same file again; it is still only one source file.
            if (!task->startedFiles.contains(file)) {
                task->startedFiles.insert(file);
                task->listedBytes += size;
                if (!task->multithreaded && task->scan) {
                    const int index = task->scan->fileIndex.value(scanKey(file), -1);
                    if (index >= 0) {
                        task->passedBytes = std::max(task->passedBytes, task->scan->fileEnds.at(index));
                    }
                }
            }
            task->filesStarted = static_cast<int>(task->startedFiles.size());
            continue;
        }
        if (line.contains(QStringLiteral("ERROR"))) {
            task->message = line;
            appendLog(QStringLiteral("[%1] %2: %3").arg(task->jobName, task->identifier, line));
            continue;
        }
        // Summary rows only; the job header also has a "Files : <pattern>" row.
        static const QRegularExpression summaryRow(QStringLiteral("^(Files|Bytes)\\s*:\\s*\\d"));
        if (summaryRow.match(line).hasMatch() || (line.startsWith(QStringLiteral("Total")) && line.contains(QStringLiteral("Copied")))) {
            appendLog(QStringLiteral("[%1] %2 \u2192 %3: %4").arg(task->jobName, task->identifier, task->destination, line.simplified()));
        }
    }
    addCopiedBytes(*task, copiedBefore);
    scheduleTasksChanged();
}

void COpyController::finishTask(int taskId, int exitCode, const QString &failure)
{
    CopyTask *task = taskById(taskId);
    if (!task || !task->process) {
        return;
    }
    const bool success = failure.isEmpty() && exitCode >= 0 && exitCode < 8;
    const qint64 copiedBefore = taskCopiedBytes(*task);
    // Every listed file is finished only when robocopy succeeded.
    if (success && task->status == TaskStatus::Running) {
        task->doneBytes = task->listedBytes;
        task->writtenBytes = task->listedBytes;
    } else {
        const qint64 written = task->process->writtenBytes();
        if (written >= 0) {
            task->writtenBytes = written;
        }
    }
    task->process->deleteLater();
    task->process = nullptr;
    task->exitCode = exitCode;
    task->elapsedMs = task->clock.elapsed();
    task->scan.reset();
    task->currentFile.clear();
    task->currentFileBytes = 0;
    task->fileProgress = 0.0;
    addCopiedBytes(*task, copiedBefore);

    if (task->status == TaskStatus::Running) {
        task->status = success ? TaskStatus::Done : TaskStatus::Failed;
        task->message = failure.isEmpty()
            ? QStringLiteral("%1 (exit code %2)").arg(robocopyExitText(exitCode)).arg(exitCode)
            : failure;
        const qint64 copied = taskCopiedBytes(*task);
        appendLog(QStringLiteral("[%1] %2 \u2192 %3: %4, %5 copied in %6%7")
            .arg(task->jobName, task->identifier, QDir::toNativeSeparators(task->destination), task->message,
                formatDataSize(copied), formatDuration(task->elapsedMs / 1000.0),
                task->elapsedMs > 0 && copied > 0 ? QStringLiteral(" at %1/s average").arg(formatDataSize(qRound64(copied * 1000.0 / task->elapsedMs)))
                                                  : QString()));
    }

    releaseFinishedManifests();
    Q_EMIT manifestsChanged();
    scheduleTasksChanged();
    // Queued: a task can fail synchronously inside start() while pumpQueue() iterates.
    QMetaObject::invokeMethod(this, &COpyController::pumpQueue, Qt::QueuedConnection);
}

void COpyController::releaseFinishedManifests()
{
    // Sources whose copies are all finished are ready again, so Start copies them once more
    // (only changed files, since robocopy skips unchanged ones).
    for (Manifest &manifest : m_manifests) {
        if (manifest.queued && std::none_of(m_tasks.cbegin(), m_tasks.cend(), [&manifest](const CopyTask &task) {
                return task.manifestId == manifest.id
                    && (task.status == TaskStatus::Pending || task.status == TaskStatus::Running || task.process);
            })) {
            manifest.queued = false;
        }
    }
}

void COpyController::updateRunning()
{
    const bool running = std::any_of(m_tasks.cbegin(), m_tasks.cend(), [](const CopyTask &task) {
        return task.status == TaskStatus::Pending || task.status == TaskStatus::Running || task.process;
    });
    if (m_running == running) {
        return;
    }
    m_running = running;
    if (running) {
        m_runClock.start();
        m_runCopiedBytes = 0;
        m_runRateSamples.clear();
        m_remainingSeconds = -1.0;
        m_copyRate = -1.0;
        m_statsTimer.start();
    } else {
        m_statsTimer.stop();
        m_runElapsedMs = m_runClock.isValid() ? m_runClock.elapsed() : 0;
        m_remainingSeconds = 0.0;
        m_copyRate = m_runElapsedMs > 0 ? m_runCopiedBytes * 1000.0 / m_runElapsedMs : -1.0;
        // Tasks started from now on belong to the next run.
        ++m_run;
    }
    Q_EMIT runningChanged();
    Q_EMIT copyStatsChanged();
    if (!running && !m_tasks.isEmpty()) {
        const int failed = failedTaskCount();
        const QString totals = QStringLiteral("in %1, %2 copied%3")
                                   .arg(formatDuration(m_runElapsedMs / 1000.0), formatDataSize(m_runCopiedBytes),
                                       m_copyRate > 0.0 ? QStringLiteral(" at %1/s average").arg(formatDataSize(qRound64(m_copyRate))) : QString());
        appendLog(failed > 0 ? QStringLiteral("Copying finished with %1 failed or stopped task(s) %2.").arg(failed).arg(totals)
                             : QStringLiteral("All copy tasks finished %1.").arg(totals));
    }
}

qint64 COpyController::taskCopiedBytes(const CopyTask &task)
{
    if (task.writtenBytes >= 0) {
        // Robocopy writes only listed files; retries can write a file twice.
        return std::min(task.writtenBytes, task.listedBytes);
    }
    return task.doneBytes + qRound64(task.fileProgress * task.currentFileBytes);
}

qint64 COpyController::taskProcessedBytes(const CopyTask &task)
{
    const qint64 copied = taskCopiedBytes(task);
    const qint64 listedLeft = std::max<qint64>(0, task.listedBytes - copied);
    return std::clamp(task.passedBytes - listedLeft, copied, std::max(task.totalBytes, copied));
}

qint64 COpyController::taskRemainingBytes(const CopyTask &task)
{
    if (!task.scan || task.multithreaded) {
        return -1;
    }
    const qint64 listedLeft = std::max<qint64>(0, task.listedBytes - taskCopiedBytes(task));
    const qint64 unvisited = std::max<qint64>(0, task.totalBytes - std::max(task.passedBytes, task.listedBytes));
    return listedLeft + qRound64(unvisited * task.changedShare);
}

double COpyController::taskRate(const CopyTask &task)
{
    return sampledRate(task.rateSamples, taskCopiedBytes(task), task.clock.isValid() ? task.clock.elapsed() : 0);
}

double COpyController::sampledRate(const RateSamples &samples, qint64 bytes, qint64 elapsedMs)
{
    // Too early to tell: robocopy may still be comparing folders.
    if (elapsedMs < 2000) {
        return -1.0;
    }
    if (!samples.isEmpty()) {
        const qint64 span = elapsedMs - samples.first().first;
        const qint64 copied = bytes - samples.first().second;
        if (span >= 2000 && copied > 0) {
            return copied * 1000.0 / span;
        }
    }
    // Nothing copied within the window (e.g. one large file with /NP): the average since start.
    return bytes > 0 ? bytes * 1000.0 / elapsedMs : -1.0;
}

qint64 COpyController::estimatedTaskBytes(const CopyTask &pending) const
{
    qint64 sum = 0;
    int count = 0;
    for (const CopyTask &task : m_tasks) {
        const bool active = task.status == TaskStatus::Running && task.process;
        if (task.run != m_run || (task.status != TaskStatus::Done && !active)) {
            continue;
        }
        qint64 expected = taskCopiedBytes(task);
        if (active) {
            const qint64 remaining = taskRemainingBytes(task);
            if (remaining < 0) {
                continue;
            }
            expected = taskCopiedBytes(task) + remaining;
        }
        // A copy of the same source to another node is the best guess.
        if (task.source == pending.source && task.filePattern == pending.filePattern && task.mirror == pending.mirror) {
            return expected;
        }
        sum += expected;
        ++count;
    }
    return count > 0 ? sum / count : -1;
}

void COpyController::addCopiedBytes(CopyTask &task, qint64 before)
{
    if (task.run == m_run) {
        m_runCopiedBytes += taskCopiedBytes(task) - before;
    }
}

void COpyController::sampleCopyStats()
{
    const qint64 now = m_runClock.elapsed();
    qint64 remainingBytes = 0;
    bool known = true;
    for (CopyTask &task : m_tasks) {
        if (task.status == TaskStatus::Running && task.process) {
            const qint64 copiedBefore = taskCopiedBytes(task);
            const qint64 written = task.process->writtenBytes();
            if (written >= 0) {
                task.writtenBytes = written;
            }
            addCopiedBytes(task, copiedBefore);
            // Changed files usually come in runs (e.g. re-rendered frames), so the share follows
            // the last ~2 % of the source: it starts at "everything changed" and adapts as
            // robocopy lists or skips files.
            const qint64 passed = task.passedBytes - task.sharePassedBytes;
            if (passed < 0) {
                // Folders turned out interleaved; measure from the new position.
                task.sharePassedBytes = task.passedBytes;
                task.shareListedBytes = task.listedBytes;
            } else if (passed > 0) {
                const double changed = std::clamp<double>(task.listedBytes - task.shareListedBytes, 0.0, passed);
                const double keep = std::exp(-passed / std::max(1.0, task.totalBytes / 50.0));
                task.changedShare = task.changedShare * keep + changed / passed * (1.0 - keep);
                task.sharePassedBytes = task.passedBytes;
                task.shareListedBytes = task.listedBytes;
            }
            const qint64 taskNow = task.clock.elapsed();
            task.rateSamples.append({ taskNow, taskCopiedBytes(task) });
            while (task.rateSamples.size() > 2 && taskNow - task.rateSamples.first().first > kRateWindowMs) {
                task.rateSamples.removeFirst();
            }
            const qint64 remaining = taskRemainingBytes(task);
            known &= remaining >= 0;
            remainingBytes += std::max<qint64>(0, remaining);
        } else if (task.status == TaskStatus::Pending) {
            const qint64 estimate = task.multithreaded ? -1 : estimatedTaskBytes(task);
            known &= estimate >= 0;
            remainingBytes += std::max<qint64>(0, estimate);
        }
    }
    m_runRateSamples.append({ now, m_runCopiedBytes });
    while (m_runRateSamples.size() > 2 && now - m_runRateSamples.first().first > kRateWindowMs) {
        m_runRateSamples.removeFirst();
    }
    // Parallel tasks share the bandwidth, so the time left is the bytes left of all tasks at
    // the rate of all tasks together.
    m_copyRate = sampledRate(m_runRateSamples, m_runCopiedBytes, now);
    if (known && remainingBytes == 0) {
        m_remainingSeconds = 0.0;
    } else {
        m_remainingSeconds = known && m_copyRate > 0.0 ? remainingBytes / m_copyRate : -1.0;
    }
    Q_EMIT copyStatsChanged();
    // Elapsed times change without robocopy output too.
    scheduleTasksChanged();
}

void COpyController::scheduleTasksChanged()
{
    if (!m_tasksChangedTimer.isActive()) {
        m_tasksChangedTimer.start();
    }
}

void COpyController::appendLog(const QString &text)
{
    qInfo().noquote() << text;
    m_logText += QTime::currentTime().toString(QStringLiteral("HH:mm:ss  ")) + text + QLatin1Char('\n');
    Q_EMIT logTextChanged();
}
