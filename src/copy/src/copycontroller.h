/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef COPYCONTROLLER_H
#define COPYCONTROLLER_H

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include "copytargets.h"

#include <atomic>
#include <memory>

class RobocopyRunner;

// Loads C-Slice copy manifests (*.c-opy.json), maps every output identifier to one or more
// destination folders (optionally from a saved or library cluster) and copies the files with
// robocopy. Like C-Troll clusters and nodes, library clusters (one JSON file each in the cluster
// folder) refer to machines by node name, and every machine keeps the node files (name and IP
// address, one JSON file each in the node folder) that match its network; {ip} and {node} in a
// destination resolve to the node assigned to the output when copying starts.
//
// Manifest outputs without their own destinations use the cluster's manifest destinations,
// else the default rule's. A cluster's nodes are the node names it assigns to outputs. Plain files
// and folders (no manifest) are copied to the default rule's destinations on every node of the
// cluster. When this computer is one of the cluster's nodes, it is skipped. Folders are always
// mirrored (/MIR), so only changed files are copied and removed files are removed.
//
// Before copying, checkTargets() checks that every node can be reached and what is already at
// each planned destination (and, for opened files and folders, the source on this computer);
// deleteTargets() deletes what was found, on the nodes and on this computer at the same time.
class COpyController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList manifests READ manifests NOTIFY manifestsChanged)
    Q_PROPERTY(int readyManifestCount READ readyManifestCount NOTIFY manifestsChanged)
    Q_PROPERTY(QVariantList destinations READ destinations NOTIFY destinationsChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString currentPreset READ currentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(bool presetModified READ presetModified NOTIFY presetModifiedChanged)
    Q_PROPERTY(bool currentPresetIsLibrary READ currentPresetIsLibrary NOTIFY currentPresetChanged)
    Q_PROPERTY(QString presetFolder READ presetFolder WRITE setPresetFolder NOTIFY libraryFoldersChanged)
    Q_PROPERTY(QString nodeFolder READ nodeFolder WRITE setNodeFolder NOTIFY libraryFoldersChanged)
    Q_PROPERTY(QString defaultPresetFolder READ defaultPresetFolder CONSTANT)
    Q_PROPERTY(QString defaultNodeFolder READ defaultNodeFolder CONSTANT)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY nodesChanged)
    Q_PROPERTY(QStringList nodeNames READ nodeNames NOTIFY nodesChanged)
    Q_PROPERTY(QStringList clusterNodes READ clusterNodes NOTIFY destinationsChanged)
    Q_PROPERTY(QString localNode READ localNode NOTIFY localNodeChanged)
    Q_PROPERTY(QString detectedLocalNode READ detectedLocalNode NOTIFY localNodeChanged)
    Q_PROPERTY(QString localNodeSetting READ localNodeSetting WRITE setLocalNodeSetting NOTIFY localNodeChanged)
    Q_PROPERTY(bool skipLocalNode READ skipLocalNode WRITE setSkipLocalNode NOTIFY localNodeChanged)
    Q_PROPERTY(bool skipHiddenFiles READ skipHiddenFiles WRITE setSkipHiddenFiles NOTIFY skipHiddenFilesChanged)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(QString startupCluster READ startupCluster WRITE setStartupCluster NOTIFY startupOptionsChanged)
    Q_PROPERTY(QVariantList robocopyProfiles READ robocopyProfiles NOTIFY robocopyProfilesChanged)
    Q_PROPERTY(QStringList robocopyProfileNames READ robocopyProfileNames NOTIFY robocopyProfilesChanged)
    Q_PROPERTY(QVariantList defaultRobocopyProfiles READ defaultRobocopyProfiles CONSTANT)
    Q_PROPERTY(QString fileRobocopyProfile READ fileRobocopyProfile WRITE setFileRobocopyProfile NOTIFY robocopyProfilesChanged)
    Q_PROPERTY(QString folderRobocopyProfile READ folderRobocopyProfile WRITE setFolderRobocopyProfile NOTIFY robocopyProfilesChanged)
    Q_PROPERTY(QString robocopyProfilesFilePath READ robocopyProfilesFilePath CONSTANT)
    Q_PROPERTY(int maxParallel READ maxParallel WRITE setMaxParallel NOTIFY maxParallelChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    // "file", "folder" or "manifest": what the user chose to copy (step 1), or the kind of the
    // source added last. Empty until something is chosen.
    Q_PROPERTY(QString sourceMode READ sourceMode WRITE setSourceMode NOTIFY sourceModeChanged)
    Q_PROPERTY(QVariantList plannedCopies READ plannedCopies NOTIFY plannedCopiesChanged)
    // Files and folders go to the same path on every node, through the cluster's drive shares
    // (e.g. D: -> \{ip}\d$), or else to the default rule's destinations.
    Q_PROPERTY(bool samePath READ samePath WRITE setSamePath NOTIFY samePathChanged)
    // Each entry: { drive: "D:", share: "\{ip}\d$" }, for the current cluster.
    Q_PROPERTY(QVariantList driveShares READ driveShares NOTIFY destinationsChanged)
    // Drives of the ready files and folders that the current cluster has no share for.
    Q_PROPERTY(QStringList unsharedDrives READ unsharedDrives NOTIFY plannedCopiesChanged)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    Q_PROPERTY(int failedTaskCount READ failedTaskCount NOTIFY tasksChanged)
    Q_PROPERTY(int finishedTaskCount READ finishedTaskCount NOTIFY tasksChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // Time of the current copy run, or of the last one when no copy runs.
    Q_PROPERTY(double elapsedSeconds READ elapsedSeconds NOTIFY copyStatsChanged)
    // Estimated time left of all running and pending tasks; -1 while unknown, and always when
    // a task copies with /MT.
    Q_PROPERTY(double remainingSeconds READ remainingSeconds NOTIFY copyStatsChanged)
    // Recent throughput of all tasks together, in bytes per second; -1 while unknown.
    Q_PROPERTY(double copyRate READ copyRate NOTIFY copyStatsChanged)
    Q_PROPERTY(qint64 copiedBytes READ copiedBytes NOTIFY copyStatsChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    // What checkTargets() found: one entry per opened file or folder on this computer
    // ("source" set) and per planned destination, see targets().
    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetsChanged)
    // Per node of the checked destinations: { node, address, state: "checking", "reachable"
    // or "unreachable", message }.
    Q_PROPERTY(QVariantList checkedNodes READ checkedNodes NOTIFY targetsChanged)
    // A check or a delete is still running.
    Q_PROPERTY(bool targetsBusy READ targetsBusy NOTIFY targetsChanged)
    // Some planned destinations or sources were not checked (the plan changed since).
    Q_PROPERTY(bool targetsStale READ targetsStale NOTIFY targetsChanged)
    Q_PROPERTY(QString settingsFilePath READ settingsFilePath CONSTANT)
    Q_PROPERTY(QUrl manifestDialogFolder READ manifestDialogFolder NOTIFY dialogFoldersChanged)
    Q_PROPERTY(QUrl sourceDialogFolder READ sourceDialogFolder NOTIFY dialogFoldersChanged)
    Q_PROPERTY(QUrl presetDialogFolder READ presetDialogFolder NOTIFY dialogFoldersChanged)
    Q_PROPERTY(QUrl destinationDialogFolder READ destinationDialogFolder NOTIFY dialogFoldersChanged)
    Q_PROPERTY(QVariantList storedAccounts READ storedAccounts NOTIFY storedAccountsChanged)
    Q_PROPERTY(QStringList storedAccountNames READ storedAccountNames NOTIFY storedAccountsChanged)
    Q_PROPERTY(bool accountsSupported READ accountsSupported CONSTANT)
    Q_PROPERTY(QString defaultAccount READ defaultAccount WRITE setDefaultAccount NOTIFY accountSettingsChanged)
    // Cluster name -> account name, for the clusters that have their own account on this computer.
    Q_PROPERTY(QVariantMap clusterAccounts READ clusterAccounts NOTIFY accountSettingsChanged)

public:
    explicit COpyController(QObject *parent = nullptr);
    ~COpyController() override;

    QVariantList manifests() const;
    int readyManifestCount() const;
    QVariantList destinations() const;
    QStringList presetNames() const;
    QVariantList presets() const;
    QString currentPreset() const;
    bool presetModified() const;
    bool currentPresetIsLibrary() const;

    // Folders with the library clusters and nodes; empty settings use the default folders.
    QString presetFolder() const;
    void setPresetFolder(const QString &folder);
    QString nodeFolder() const;
    void setNodeFolder(const QString &folder);
    QString defaultPresetFolder() const;
    QString defaultNodeFolder() const;

    QVariantList nodes() const;
    QStringList nodeNames() const;

    // Distinct node names the current mapping assigns to outputs, in a stable order.
    QStringList clusterNodes() const;

    // The node that is this computer: the LocalNode setting, or else the node whose name is the
    // host name or whose address belongs to this computer. Empty when none matches.
    QString localNode() const;
    QString detectedLocalNode() const;
    QString localNodeSetting() const;
    void setLocalNodeSetting(const QString &name);
    bool skipLocalNode() const;
    void setSkipLocalNode(bool skip);
    bool skipHiddenFiles() const;
    void setSkipHiddenFiles(bool skip);
    QString hostName() const;

    // Empty: keep the last used cluster on startup.
    QString startupCluster() const;
    void setStartupCluster(const QString &name);

    // Each profile: { name, arguments, description }. Stored in robocopyProfilesFilePath().
    QVariantList robocopyProfiles() const;
    QStringList robocopyProfileNames() const;
    QVariantList defaultRobocopyProfiles() const;
    // Profile for single files (opened files, movie outputs) and for folders (opened folders,
    // image sequence outputs). Unknown names fall back to the first profile.
    QString fileRobocopyProfile() const;
    void setFileRobocopyProfile(const QString &name);
    QString folderRobocopyProfile() const;
    void setFolderRobocopyProfile(const QString &name);
    QString robocopyProfilesFilePath() const;

    QString sourceMode() const;
    void setSourceMode(const QString &mode);

    // What Start copy would queue for the ready sources: one entry per source, node and
    // destination, with "skipped" set to the reason when it would not be copied. "key" names
    // the entry for setPlannedCopyEnabled(); "disabled" is set when the user turned it off.
    QVariantList plannedCopies() const;

    bool samePath() const;
    void setSamePath(bool samePath);
    QVariantList driveShares() const;
    QStringList unsharedDrives() const;

    int maxParallel() const;
    void setMaxParallel(int maxParallel);

    bool autoStart() const;
    void setAutoStart(bool autoStart);

    QVariantList tasks() const;
    int failedTaskCount() const;
    int finishedTaskCount() const;
    bool running() const;
    double elapsedSeconds() const;
    double remainingSeconds() const;
    double copyRate() const;
    qint64 copiedBytes() const;
    QString logText() const;
    QString settingsFilePath() const;

    // Each: { id, source, jobName, identifier, node, path, wholeFolder, accountName, state,
    // fileCount, folderCount, totalBytes, message }. state is "checking", "deleting",
    // "unreachable", "missing" or "present".
    QVariantList targets() const;
    QVariantList checkedNodes() const;
    bool targetsBusy() const;
    bool targetsStale() const;

    // Folders the file dialogs open in; the last chosen folder is remembered in the settings.
    QUrl manifestDialogFolder() const;
    QUrl sourceDialogFolder() const;
    QUrl presetDialogFolder() const;
    QUrl destinationDialogFolder() const;

    QVariantList storedAccounts() const;
    QStringList storedAccountNames() const;
    bool accountsSupported() const;

    // Accounts kept by this computer, not by clusters. An output copies with its own account,
    // else the cluster's default rule account, else this computer's account for the current
    // cluster, else the default account, else as the current Windows user.
    QString defaultAccount() const;
    void setDefaultAccount(const QString &accountName);
    QVariantMap clusterAccounts() const;
    // Empty accountName: the cluster uses the default account.
    Q_INVOKABLE void setClusterAccount(const QString &clusterName, const QString &accountName);

    // Opens a fresh selection, replacing the source preview and clearing the previous checks.
    // An empty mode detects manifests and clusters; "file" and "manifest" match the dialogs.
    Q_INVOKABLE bool openPaths(const QStringList &paths, const QString &mode = QString());
    Q_INVOKABLE bool openPath(const QString &pathOrUrl);
    // Low-level additions used to assemble a selection without replacing its other sources.
    Q_INVOKABLE bool loadManifest(const QString &pathOrUrl);
    // Adds a file or folder that is copied to every node of the cluster.
    Q_INVOKABLE bool addSource(const QString &pathOrUrl);
    Q_INVOKABLE void removeManifest(int index);
    // Turns a planned copy (by its "key" in plannedCopies) off or on again; a disabled copy is
    // skipped by Start copy. Kept until the source is removed, not saved.
    Q_INVOKABLE void setPlannedCopyEnabled(const QString &key, bool enabled);

    // Replaces all robocopy profiles (from the settings dialog) and writes the profile file.
    Q_INVOKABLE void setRobocopyProfiles(const QVariantList &profiles);

    Q_INVOKABLE void addDestination(const QString &identifier, const QString &path = QString());
    Q_INVOKABLE void setDestination(const QString &identifier, int index, const QString &path);
    Q_INVOKABLE void removeDestination(const QString &identifier, int index);
    // Empty accountName: the output uses the default rule's account (or the current user).
    Q_INVOKABLE void setOutputAccount(const QString &identifier, const QString &accountName);
    // Empty nodeName: the output uses the default rule's node (or none).
    Q_INVOKABLE void setOutputNode(const QString &identifier, const QString &nodeName);
    // drive: "D", "D:" or "D:\". An empty share removes the drive from the current cluster.
    Q_INVOKABLE void setDriveShare(const QString &drive, const QString &share);

    // Node files are rewritten in place, so they keep keys C-Opy does not use (e.g. C-Troll's port).
    Q_INVOKABLE bool setNodeAddress(const QString &name, const QString &ipAddress);
    Q_INVOKABLE bool addNode(const QString &name, const QString &ipAddress, const QString &description = QString());
    Q_INVOKABLE void reloadLibrary();
    Q_INVOKABLE void openPresetFolder() const;
    Q_INVOKABLE void openNodeFolder() const;

    Q_INVOKABLE bool storeAccount(const QString &name, const QString &userName, const QString &password);
    Q_INVOKABLE void removeAccount(const QString &name);
    Q_INVOKABLE void refreshAccounts();

    Q_INVOKABLE void applyPreset(const QString &name);
    Q_INVOKABLE void savePreset(const QString &name);
    Q_INVOKABLE void deletePreset(const QString &name);
    Q_INVOKABLE bool importPresets(const QString &pathOrUrl);

    Q_INVOKABLE void startCopy();
    Q_INVOKABLE void stopCopy();
    Q_INVOKABLE void retryFailed();
    Q_INVOKABLE void clearFinished();

    // Checks the nodes and the planned destinations of the ready sources, in the background.
    Q_INVOKABLE void checkTargets();
    // Deletes checked targets (by "id" in targets) that are present, then checks them again.
    // A deleted source is removed from the sources. Refused while copying.
    Q_INVOKABLE void deleteTargets(const QVariantList &ids);

    Q_INVOKABLE void clearLog();
    // "1:05" or "1:02:03"; for elapsed and remaining copy times.
    Q_INVOKABLE QString formatDuration(double seconds) const;
    Q_INVOKABLE QString formatDataSize(qint64 bytes) const;
    Q_INVOKABLE void openSettingsFolder() const;
    Q_INVOKABLE QUrl presetExamplesFolder() const;
    Q_INVOKABLE QString urlToLocalPath(const QUrl &url) const;
    // Each takes the chosen file or folder and stores the folder it is in (or is).
    Q_INVOKABLE void rememberManifestDialogFolder(const QUrl &url);
    Q_INVOKABLE void rememberSourceDialogFolder(const QUrl &url, bool isFile);
    Q_INVOKABLE void rememberPresetDialogFolder(const QUrl &url);
    Q_INVOKABLE void rememberDestinationDialogFolder(const QUrl &url);

Q_SIGNALS:
    void manifestsChanged();
    void destinationsChanged();
    void presetsChanged();
    void currentPresetChanged();
    void presetModifiedChanged();
    void robocopyProfilesChanged();
    void localNodeChanged();
    void skipHiddenFilesChanged();
    void startupOptionsChanged();
    void sourceModeChanged();
    void plannedCopiesChanged();
    void samePathChanged();
    void maxParallelChanged();
    void autoStartChanged();
    void tasksChanged();
    void runningChanged();
    void copyStatsChanged();
    void logTextChanged();
    void storedAccountsChanged();
    void accountSettingsChanged();
    void dialogFoldersChanged();
    void libraryFoldersChanged();
    void nodesChanged();
    void targetsChanged();

private:
    using DestinationMap = QMap<QString, QStringList>;
    // Output identifier ("*" for the default rule) -> name of a stored account.
    using AccountMap = QMap<QString, QString>;
    // Output identifier ("*" for the default rule) -> node name.
    using NodeMap = QMap<QString, QString>;
    // Drive ("D:") -> share template on every node, e.g. "\{ip}\d$".
    using DriveShareMap = QMap<QString, QString>;

    struct ManifestOutput {
        QString identifier;
        QString sourceDirectory;
        QString filePattern;
    };

    // Manifest: outputs mapped by identifier. File and Folder: copied to every cluster node.
    enum class SourceKind { Manifest, File, Folder };

    struct Manifest {
        int id = 0;
        SourceKind kind = SourceKind::Manifest;
        QString path;
        QString jobName;
        QString generator;
        QString created;
        QString outputRoot;
        QList<ManifestOutput> outputs;
        bool queued = false;
    };

    struct Preset {
        QString name;
        QString description;
        DestinationMap destinations;
        AccountMap accounts;
        NodeMap nodes;
        DriveShareMap driveShares;
        // Set for library clusters: the JSON file in the cluster folder. Library clusters are
        // read-only in C-Opy and not written to the settings file.
        QString filePath;
    };

    struct Node {
        QString name;
        QString ipAddress;
        QString description;
        QString filePath;
    };

    struct PlannedCopy {
        int manifestId = 0;
        QString jobName;
        QString identifier;
        QString node;
        QString source;
        QString destination;
        QString filePattern;
        QString accountName;
        QString profileName;
        bool mirror = true;
        // Not empty: why this copy is not made. local: the node is this computer.
        QString skipped;
        bool local = false;
        bool disabled = false;
    };

    struct RobocopyProfile {
        QString name;
        QString arguments;
        QString description;
    };

    enum class TaskStatus { Pending, Running, Done, Failed, Stopped };

    // A planned destination or an opened source, as found by checkTargets().
    struct CheckedTarget {
        int id = 0;
        int manifestId = 0;
        // The opened file or folder on this computer.
        bool source = false;
        QString jobName;
        QString identifier;
        QString node;
        COpyTargets::Target target;
        // Empty when idle, else "checking" or "deleting".
        QString action;
        bool checked = false;
        COpyTargets::Inspection inspection;
        QString message;
    };

    // The files a task copies, in the order robocopy visits them: the files of a folder, then
    // its subfolders depth-first, each in directory order. Robocopy does not list unchanged
    // files, so the position of a listed file also tells which files it has passed. Only
    // counted for /MT, which lists files when it queues them and works on several folders at
    // once, so the position tells nothing there.
    struct SourceScan {
        int fileCount = 0;
        qint64 totalBytes = 0;
        // Case-folded clean path -> index into fileEnds.
        QHash<QString, int> fileIndex;
        // Bytes of all files up to and including each file.
        QList<qint64> fileEnds;
    };

    // (milliseconds since start, bytes copied), one per second over the last rate window.
    using RateSamples = QList<QPair<qint64, qint64>>;

    struct CopyTask {
        int id = 0;
        int manifestId = 0;
        QString jobName;
        QString identifier;
        QString source;
        QString destination;
        QString filePattern;
        QString accountName;
        // False for a single file: robocopy still skips it when unchanged, but nothing is purged.
        bool mirror = true;
        bool skipHidden = false;
        QString profileName;
        QString profileArguments;
        TaskStatus status = TaskStatus::Pending;
        int exitCode = -1;
        int totalFiles = 0;
        int filesStarted = 0;
        QSet<QString> startedFiles;
        double fileProgress = 0.0;
        // Released when the task ends; the totals stay.
        QSharedPointer<const SourceScan> scan;
        qint64 totalBytes = 0;
        // Bytes of the files robocopy listed to copy. With /MT it lists files when it queues
        // them, ahead of the copy.
        qint64 listedBytes = 0;
        // Bytes written so far, sampled from robocopy's I/O counters; -1 when unknown.
        qint64 writtenBytes = -1;
        // Without I/O counters: the listed files that are finished and the current one.
        qint64 doneBytes = 0;
        QString currentFile;
        qint64 currentFileBytes = 0;
        // Source bytes robocopy has passed, listed or skipped, including the last listed file.
        qint64 passedBytes = 0;
        // /MT in the profile: the speed is measured, but no time left is estimated.
        bool multithreaded = false;
        // Share of the recently passed bytes that robocopy listed rather than skipped as
        // unchanged; the bytes not passed yet are expected to behave alike.
        double changedShare = 1.0;
        qint64 sharePassedBytes = 0;
        qint64 shareListedBytes = 0;
        int run = 0;
        QElapsedTimer clock;
        qint64 elapsedMs = 0;
        RateSamples rateSamples;
        QString message;
        QString outputBuffer;
        RobocopyRunner *process = nullptr;
    };

    static QList<Preset> presetsFromJson(const QJsonObject &root);
    // indexFiles: also keep every file's path and position, for the time-left estimate.
    static QSharedPointer<const SourceScan> scanSource(const QString &source, const QString &pattern, bool recursive, bool skipHidden, bool indexFiles);
    static QList<RobocopyProfile> builtInRobocopyProfiles();

    void loadSettings();
    void saveSettings() const;
    void loadLibraryPresets();
    void loadNodes();
    void loadRobocopyProfiles();
    bool saveRobocopyProfiles();
    void applyStartupOptions();
    void updateLocalNode();
    const RobocopyProfile *findRobocopyProfile(const QString &name) const;
    // The configured profile, or the first one when it no longer exists.
    const RobocopyProfile &robocopyProfileFor(bool singleFile) const;
    bool isLocalNode(const QString &name) const;
    QString accountForNode(const QString &nodeName) const;
    // The account the settings give the current cluster: its own, else the default account.
    QString settingsAccount() const;
    // The default rule's account, else settingsAccount().
    QString defaultRuleAccount() const;
    static QStringList presetClusterNodes(const NodeMap &nodes);
    QList<PlannedCopy> planCopies() const;
    static QString plannedCopyKey(const PlannedCopy &copy);
    // Applies the current library cluster's file again when it changed and the working mapping
    // was not edited since the cluster was applied.
    void syncCurrentLibraryPreset();
    void seedNodeFolder() const;
    void destinationsEdited();
    int presetIndex(const QString &name) const;
    const Node *findNode(const QString &name) const;
    QString effectiveNodeName(const QString &identifier) const;
    QString manifestStatus(const Manifest &manifest) const;
    bool manifestBusy(int manifestId) const;
    // Returns an empty string and sets *error when a node token cannot be resolved. nodeName
    // overrides the output's node for {ip} and {node}; usedNodes receives every node resolved.
    QString expandDestination(const QString &destination, const QString &folderName, const QString &jobName,
        const QString &identifier, const QString &nodeName, QStringList *usedNodes, QString *error) const;

    // Bytes copied so far, including partly copied files.
    static qint64 taskCopiedBytes(const CopyTask &task);
    // Source bytes robocopy has passed, without the listed bytes still to copy.
    static qint64 taskProcessedBytes(const CopyTask &task);
    // Estimated bytes the task still has to copy; -1 without a source scan or with /MT.
    static qint64 taskRemainingBytes(const CopyTask &task);
    static double taskRate(const CopyTask &task);
    // Bytes per second over the samples, or else the average since start; -1 while unknown.
    static double sampledRate(const RateSamples &samples, qint64 bytes, qint64 elapsedMs);
    // What a pending task will likely copy, from started tasks; -1 while nothing is known.
    qint64 estimatedTaskBytes(const CopyTask &pending) const;
    void sampleCopyStats();
    void addCopiedBytes(CopyTask &task, qint64 before);

    CopyTask *taskById(int id);
    void pumpQueue();
    void startTask(CopyTask &task);
    void handleTaskOutput(int taskId, const QString &text);
    void finishTask(int taskId, int exitCode, const QString &failure = QString());
    // Marks sources without pending or running tasks as ready again.
    void releaseFinishedManifests();
    void updateRunning();
    void scheduleTasksChanged();
    void appendLog(const QString &text);

    static COpyTargets::Target copyTarget(const PlannedCopy &copy);
    static COpyTargets::Target sourceTarget(const Manifest &manifest);
    static QString targetKey(const QString &node, const COpyTargets::Target &target);
    static QString targetPath(const COpyTargets::Target &target);
    // "this computer", the node, or "the destination" for one without a node.
    static QString targetLocation(const CheckedTarget &target);
    // Starts a check (or a delete, then a check) of the target on the worker threads.
    void runTargetJob(CheckedTarget &target, bool remove);
    void finishTargetJob(int targetId, bool removed, const COpyTargets::Inspection &inspection, const QString &error);

    QList<Manifest> m_manifests;
    QList<Preset> m_presets;
    QList<CopyTask> m_tasks;
    QList<Node> m_nodes;
    QList<RobocopyProfile> m_robocopyProfiles;
    DestinationMap m_destinations;
    AccountMap m_accounts;
    // Keys of the planned copies the user disabled.
    QSet<QString> m_disabledCopies;
    NodeMap m_outputNodes;
    DriveShareMap m_driveShares;
    QList<QPair<QString, QString>> m_storedAccounts; // name, user name
    // Cluster name -> account name; [ClusterAccounts] in the settings file.
    QMap<QString, QString> m_clusterAccounts;
    QString m_currentPreset;
    // mappingSignature() of the current cluster when it was last applied or saved.
    QString m_appliedSignature;
    QString m_sourceMode;
    QString m_detectedLocalNode;
    int m_maxParallel = 4;
    bool m_autoStart = false;
    bool m_running = false;
    int m_nextManifestId = 0;
    int m_nextTaskId = 0;
    QString m_logText;
    QTimer m_tasksChangedTimer;
    // Samples the throughput once per second while copying.
    QTimer m_statsTimer;
    QElapsedTimer m_runClock;
    qint64 m_runElapsedMs = 0;
    qint64 m_runCopiedBytes = 0;
    RateSamples m_runRateSamples;
    double m_remainingSeconds = -1.0;
    double m_copyRate = -1.0;
    int m_run = 0;
    QList<CheckedTarget> m_targets;
    int m_nextTargetId = 0;
    // Set when the controller goes away, so running checks and deletes stop early.
    std::shared_ptr<std::atomic_bool> m_targetsCancelled = std::make_shared<std::atomic_bool>(false);
    // Checks and deletes wait for the network, so they get threads of their own.
    QThreadPool m_targetPool;
};

#endif // COPYCONTROLLER_H
