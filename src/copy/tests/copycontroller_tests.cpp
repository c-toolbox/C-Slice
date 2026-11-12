/*
 * SPDX-FileCopyrightText: 2026 Erik Sundén
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "copycontroller.h"
#include "copysettings.h"

#include <KSharedConfig>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class CopyControllerTests : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> fixture;
    QStringList junctions;

    QString path(const QString &relative) const { return fixture->filePath(relative); }

    static void writeFile(const QString &name, const QByteArray &bytes)
    {
        QVERIFY(QDir().mkpath(QFileInfo(name).absolutePath()));
        QFile file(name);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }

    QString manifest(const QString &name = QStringLiteral("job.json"))
    {
        const QString nameOnDisk = path(name);
        const QJsonObject output { { "identifier", "output" }, { "sourceDirectory", path("source") }, { "filePattern", "*" } };
        writeFile(nameOnDisk, QJsonDocument(QJsonObject { { "format", "c-opy-manifest" }, { "outputs", QJsonArray { output } } }).toJson());
        return nameOnDisk;
    }

    static QMap<QString, QByteArray> inventory(const QString &folder)
    {
        QMap<QString, QByteArray> result;
        QDirIterator files(folder, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            QFile file(files.next());
            if (file.open(QIODevice::ReadOnly)) {
                result.insert(QDir(folder).relativeFilePath(file.fileName()), file.readAll());
            }
        }
        return result;
    }

    static int readyCopies(const COpyController &controller)
    {
        int count = 0;
        for (const QVariant &entry : controller.plannedCopies()) {
            count += entry.toMap().value("skipped").toString().isEmpty();
        }
        return count;
    }

    int junction(const QString &link, const QString &target)
    {
        const int exitCode = QProcess::execute("cmd.exe", { "/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target) });
        if (exitCode == 0) {
            junctions << link;
        }
        return exitCode;
    }

private Q_SLOTS:
    void init()
    {
#ifndef Q_OS_WIN
        QSKIP("Robocopy integration tests require Windows.");
#endif
        fixture = std::make_unique<QTemporaryDir>();
        QVERIFY(fixture->isValid());
        COpySettings::self()->setSharedConfig(KSharedConfig::openConfig(path("copy.conf"), KConfig::SimpleConfig));
        COpySettings::self()->setDefaults();
        COpySettings::setPresetFolder(path("clusters"));
        COpySettings::setNodeFolder(path("nodes"));
        QVERIFY(QDir().mkpath(path("clusters")));
        QVERIFY(QDir().mkpath(path("nodes")));
    }

    void cleanup()
    {
        // Even a failed assertion must leave no junction cycle for fixture cleanup.
        for (const QString &link : std::as_const(junctions)) {
            QVERIFY(QDir().rmdir(link));
        }
        junctions.clear();
    }

    void thousandFilesMultipleDestinationsAndRepeat()
    {
        for (int i = 0; i < 1000; ++i) {
            writeFile(path(QStringLiteral("source/dir%1/file%2.dat").arg(i / 100).arg(i % 100)), QByteArray::number(i));
        }
        writeFile(path("cluster.json"), R"({"name":"test-cluster","nodes":{"*":"remote"}})");
        COpyController controller;
        const QString sourceManifest = manifest();
        QVERIFY(controller.importPresets(path("cluster.json")));
        controller.setSamePath(false);
        QVERIFY(controller.addSource(path("source")));
        controller.addDestination("*", path("copy-a"));
        controller.addDestination("*", path("copy-b"));
        controller.addDestination("*", path("copy-a/./"));
        controller.addDestination("*", QDir::toNativeSeparators(path("COPY-A")));
        QVERIFY(controller.loadManifest(manifest("duplicate.json")));
        QCOMPARE(readyCopies(controller), 2);
        controller.startCopy();
        QCOMPARE(controller.tasks().size(), 2);
        // Adding the same source while its copies run must not race them.
        QVERIFY(controller.loadManifest(manifest("while-running.json")));
        QCOMPARE(readyCopies(controller), 0);
        controller.startCopy();
        QCOMPARE(controller.tasks().size(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        const auto original = inventory(path("source"));
        QCOMPARE(original.size(), 1000);
        for (const QString &destination : { path("copy-a"), path("copy-b") }) {
            QCOMPARE(inventory(destination), original);
        }
        qint64 sourceBytes = 0;
        for (const QByteArray &contents : original) {
            sourceBytes += contents.size();
        }
        for (const QVariant &entry : controller.tasks()) {
            QCOMPARE(entry.toMap().value("totalFiles").toInt(), 1000);
            QCOMPARE(entry.toMap().value("filesStarted").toInt(), 1000);
            QCOMPARE(entry.toMap().value("totalBytes").toLongLong(), sourceBytes);
            QCOMPARE(entry.toMap().value("copiedBytes").toLongLong(), sourceBytes);
            QCOMPARE(entry.toMap().value("progress").toDouble(), 1.0);
            QVERIFY(entry.toMap().value("elapsedSeconds").toDouble() > 0.0);
        }
        QCOMPARE(controller.copiedBytes(), 2 * sourceBytes);
        QCOMPARE(controller.remainingSeconds(), 0.0);
        QVERIFY(controller.elapsedSeconds() > 0.0);
        QVERIFY(controller.logText().contains("Total Copied Skipped Mismatch FAILED Extras"));

        // Unchanged files must not be copied on a second run.
        while (!controller.manifests().isEmpty()) {
            controller.removeManifest(0);
        }
        controller.clearLog();
        QVERIFY(controller.loadManifest(sourceManifest));
        controller.startCopy();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        QCOMPARE(controller.logText().count(QRegularExpression("Files : 1000 0 1000 0 0 0")), 2);
        QCOMPARE(controller.copiedBytes(), 0);

        // One changed file, one removed file and one extra destination file.
        writeFile(path("source/dir0/file0.dat"), "changed contents");
        QVERIFY(QFile::remove(path("source/dir0/file1.dat")));
        writeFile(path("copy-a/extra.dat"), "extra");
        writeFile(path("copy-b/extra.dat"), "extra");
        controller.removeManifest(0);
        controller.clearLog();
        QVERIFY(controller.loadManifest(sourceManifest));
        controller.startCopy();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        for (const QString &destination : { path("copy-a"), path("copy-b") }) {
            QCOMPARE(inventory(destination), inventory(path("source")));
        }
        QCOMPARE(controller.logText().count(QRegularExpression("Files : 999 1 998 0 0 2")), 2);
        QCOMPARE(controller.copiedBytes(), 2 * qint64(sizeof("changed contents") - 1));
    }

    void openingSelectionReplacesPreviewAndChecks_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("mode");
        for (const QString &kind : { QString("file"), QString("folder"), QString("manifest") }) {
            QTest::newRow(qPrintable(kind + "-auto")) << kind << QString();
            if (kind != "folder") {
                QTest::newRow(qPrintable(kind + "-dialog")) << kind << kind;
            }
        }
    }

    void openingSelectionReplacesPreviewAndChecks()
    {
        QFETCH(QString, kind);
        QFETCH(QString, mode);
        writeFile(path("source/old.txt"), "old");
        writeFile(path("new-source/new.txt"), "new");
        writeFile(path("nodes/remote.json"), R"({"name":"remote","ip":"127.0.0.2"})");
        writeFile(path("cluster.json"), QJsonDocument(QJsonObject {
            { "name", "test-cluster" }, { "nodes", QJsonObject { { "*", "remote" } } },
            { "destinations", QJsonObject { { "*", path("copy/{node}/{identifier}") } } },
        }).toJson());
        COpyController controller;
        QVERIFY(controller.importPresets(path("cluster.json")));
        controller.applyPreset("test-cluster");
        controller.setSamePath(false);
        QVERIFY(controller.openPath(manifest()));
        controller.checkTargets();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.targetsBusy(), 30000);
        QVERIFY(!controller.targets().isEmpty());
        QVERIFY(!controller.checkedNodes().isEmpty());
        controller.setPlannedCopyEnabled(controller.plannedCopies().first().toMap().value("key").toString(), false);

        QString next = path("new-source/new.txt");
        if (kind == "folder") {
            next = path("new-source");
        } else if (kind == "manifest") {
            next = path("new.c-opy.json");
            writeFile(next, QJsonDocument(QJsonObject {
                { "format", "c-opy-manifest" },
                { "outputs", QJsonArray { QJsonObject { { "identifier", "new-output" }, { "sourceDirectory", path("new-source") } } } },
            }).toJson());
        }
        QVERIFY(controller.openPaths({ QUrl::fromLocalFile(next).toString() }, mode));
        QCOMPARE(controller.manifests().size(), 1);
        QCOMPARE(controller.sourceMode(), kind);
        QCOMPARE(controller.currentPreset(), QString("test-cluster"));
        QVERIFY(!controller.presetModified());
        QCOMPARE(controller.plannedCopies().size(), 1);
        QCOMPARE(readyCopies(controller), 1);
        QVERIFY(controller.plannedCopies().first().toMap().value("identifier").toString().startsWith("new"));
        for (const QVariant &rule : controller.destinations()) {
            QVERIFY(rule.toMap().value("identifier").toString() != "output");
        }
        QVERIFY(controller.targets().isEmpty());
        QVERIFY(controller.checkedNodes().isEmpty());
        QVERIFY(!controller.targetsBusy());
        QVERIFY(!controller.targetsStale());
        controller.checkTargets();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.targetsBusy(), 30000);
        QCOMPARE(controller.targets().size(), kind == "manifest" ? 1 : 2);
    }

    void openingBatchAndReopeningClearsChecksButInvalidInputKeepsSelection()
    {
        writeFile(path("old/old.txt"), "old");
        writeFile(path("new/a.txt"), "a");
        writeFile(path("new/b.txt"), "b");
        COpyController controller;
        QVERIFY(controller.openPath(path("old")));
        // Replace the selection while its check still has a queued completion.
        controller.checkTargets();
        QVERIFY(controller.targetsBusy());
        QVERIFY(controller.openPaths({ path("new/a.txt"), path("missing"), path("new/b.txt") }, "file"));
        QCOMPARE(controller.manifests().size(), 2);
        QVERIFY(controller.targets().isEmpty());
        controller.checkTargets();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.targetsBusy(), 30000);
        QCOMPARE(controller.targets().size(), 2);
        for (const QVariant &entry : controller.targets()) {
            QVERIFY(entry.toMap().value("path").toString().contains("new"));
        }

        QVERIFY(!controller.openPath(path("missing")));
        QCOMPARE(controller.manifests().size(), 2);
        QCOMPARE(controller.targets().size(), 2);
        writeFile(path("invalid.json"), "{}");
        QVERIFY(!controller.openPaths({ path("invalid.json") }, "manifest"));
        QCOMPARE(controller.targets().size(), 2);

        // An already-open file is still a fresh selection and clears the previous check.
        QVERIFY(controller.openPath(path("new/a.txt")));
        QCOMPARE(controller.manifests().size(), 1);
        QVERIFY(controller.targets().isEmpty());

        writeFile(path("cluster.json"), R"({"name":"test-cluster","nodes":{"*":"remote"}})");
        QVERIFY(controller.openPath(path("cluster.json")));
        QCOMPARE(controller.manifests().size(), 1);

        // The same JSON can be opened as a manifest or chosen as a file to copy.
        const QString json = manifest();
        QVERIFY(controller.openPath(json));
        QVERIFY(controller.openPaths({ json }, "file"));
        QCOMPARE(controller.manifests().size(), 1);
        QCOMPARE(controller.sourceMode(), QString("file"));
        QVERIFY(controller.openPath(json));
        QCOMPARE(controller.manifests().size(), 1);
        QCOMPARE(controller.sourceMode(), QString("manifest"));
    }

    void openingSelectionKeepsExistingCopyTasks()
    {
        writeFile(path("source/a.txt"), "a");
        writeFile(path("new/b.txt"), "b");
        COpyController controller;
        QVERIFY(controller.openPath(manifest()));
        controller.addDestination("*", path("copy"));
        controller.startCopy();
        QVERIFY(controller.running());
        QCOMPARE(controller.tasks().size(), 1);
        QVERIFY(controller.openPath(path("new/b.txt")));
        QCOMPARE(controller.manifests().size(), 1);
        QCOMPARE(controller.tasks().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        QCOMPARE(inventory(path("copy")), inventory(path("source")));
        QCOMPARE(controller.manifests().size(), 1);
        QCOMPARE(controller.readyManifestCount(), 1);
    }

    void overlappingPaths_data()
    {
        QTest::addColumn<QString>("destination");
        QTest::addColumn<bool>("rejected");
        QTest::newRow("self") << QStringLiteral("source") << true;
        QTest::newRow("child") << QStringLiteral("source/backup") << true;
        QTest::newRow("parent") << QStringLiteral(".") << true;
        QTest::newRow("normalized-child") << QStringLiteral("source/other/../backup") << true;
        QTest::newRow("drive-root-with-dot") << QStringLiteral("<root>/.") << true;
        QTest::newRow("share-root-with-dot") << QStringLiteral(R"(\\unreachable.invalid\share\.)") << true;
        QTest::newRow("sibling-with-same-prefix") << QStringLiteral("source-other") << false;
    }

    void overlappingPaths()
    {
        QFETCH(QString, destination);
        QFETCH(bool, rejected);
        writeFile(path("source/example.txt"), "source");
        COpyController controller;
        QVERIFY(controller.loadManifest(manifest()));
        if (destination.startsWith("<root>")) {
            destination.replace("<root>", QDir(path("source")).rootPath());
        } else if (!destination.startsWith("\\\\")) {
            destination = path(destination);
        }
        controller.addDestination("*", destination);
        QCOMPARE(controller.plannedCopies().size(), 1);
        QCOMPARE(readyCopies(controller), rejected ? 0 : 1);
        if (rejected) {
            controller.startCopy();
            QVERIFY(controller.tasks().isEmpty());
            QCOMPARE(inventory(path("source")).size(), 1);
        }
    }

    void accountFallbacks()
    {
        writeFile(path("source/example.txt"), "source");
        writeFile(path("cluster.json"), R"({"name":"test-cluster","nodes":{"*":"remote"}})");
        {
            COpyController controller;
            QVERIFY(controller.importPresets(path("cluster.json")));
            controller.addDestination("*", path("copy"));
            QVERIFY(controller.loadManifest(manifest()));
            const auto account = [&controller] {
                return controller.plannedCopies().constFirst().toMap().value("accountName").toString();
            };
            QCOMPARE(account(), QString());
            controller.setDefaultAccount("everywhere");
            QCOMPARE(account(), QString("everywhere"));
            controller.setClusterAccount("other-cluster", "other");
            QCOMPARE(account(), QString("everywhere"));
            controller.setClusterAccount("test-cluster", "cluster");
            QCOMPARE(account(), QString("cluster"));
            controller.setOutputAccount("*", "rule");
            QCOMPARE(account(), QString("rule"));
            controller.setOutputAccount("output", "own");
            QCOMPARE(account(), QString("own"));
            controller.setClusterAccount("other-cluster", QString());
        }
        COpyController reloaded;
        QCOMPARE(reloaded.defaultAccount(), QString("everywhere"));
        QCOMPARE(reloaded.clusterAccounts(), (QVariantMap { { "test-cluster", "cluster" } }));
    }

    void junctionsAndAliases()
    {
        writeFile(path("source/local.txt"), "local");
        writeFile(path("external/foreign.txt"), "foreign");
        const QString externalLink = path("source/linked");
        const QString cycle = path("source/cycle");
        const QString alias = path("alias");
        QCOMPARE(junction(externalLink, path("external")), 0);
        QCOMPARE(junction(cycle, path("source")), 0);
        QCOMPARE(junction(alias, path("source")), 0);
        COpyController controller;
        QVERIFY(controller.loadManifest(manifest()));
        controller.addDestination("*", path("copy"));
        controller.addDestination("*", path("alias/new-destination"));
        QCOMPARE(readyCopies(controller), 1);
        controller.startCopy();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        QCOMPARE(controller.tasks().first().toMap().value("totalFiles").toInt(), 1);
        QCOMPARE(inventory(path("copy")).size(), 1);
        QVERIFY(!QFileInfo::exists(path("copy/linked/foreign.txt")));
        QVERIFY(!QFileInfo::exists(path("copy/cycle")));
    }

    void hiddenFileCounts_data()
    {
        QTest::addColumn<bool>("skipHidden");
        QTest::addColumn<int>("expected");
        QTest::newRow("include-all") << false << 4;
        QTest::newRow("skip-hidden-system") << true << 2;
    }

    void hiddenFileCounts()
    {
        QFETCH(bool, skipHidden);
        QFETCH(int, expected);
        writeFile(path("source/visible.txt"), "visible");
        writeFile(path("source/hidden.txt"), "hidden");
        writeFile(path("source/system.txt"), "system");
        writeFile(path("source/hidden-folder/visible.txt"), "inside hidden folder");
#ifdef Q_OS_WIN
        QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>(path("source/hidden.txt").utf16()), FILE_ATTRIBUTE_HIDDEN));
        QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>(path("source/system.txt").utf16()), FILE_ATTRIBUTE_SYSTEM));
        QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>(path("source/hidden-folder").utf16()), FILE_ATTRIBUTE_HIDDEN));
#endif
        COpyController controller;
        controller.setSkipHiddenFiles(skipHidden);
        QVERIFY(controller.loadManifest(manifest()));
        controller.addDestination("*", path("copy"));
        controller.startCopy();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        QCOMPARE(controller.tasks().first().toMap().value("totalFiles").toInt(), expected);
        QCOMPARE(inventory(path("copy")).size(), expected);
    }

    void singleFileKeepsUnrelatedDestinationFiles()
    {
        writeFile(path("source/movie.mp4"), "movie");
        writeFile(path("copy/keep.txt"), "keep");
        writeFile(path("cluster.json"), QJsonDocument(QJsonObject {
            { "name", "test-cluster" }, { "nodes", QJsonObject { { "*", "remote" } } },
            { "destinations", QJsonObject { { "*", path("copy") } } },
        }).toJson());
        COpyController controller;
        QVERIFY(controller.importPresets(path("cluster.json")));
        controller.applyPreset("test-cluster");
        controller.setSamePath(false);
        QVERIFY(controller.addSource(path("source/movie.mp4")));
        QCOMPARE(readyCopies(controller), 1);
        controller.startCopy();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        QCOMPARE(inventory(path("copy")).size(), 2);
        QCOMPARE(controller.tasks().first().toMap().value("totalFiles").toInt(), 1);
    }

    void manifestDestinationsComeBeforeTheDefaultRule()
    {
        writeFile(path("source/example.txt"), "source");
        writeFile(path("cluster.json"), QJsonDocument(QJsonObject {
            { "name", "test-cluster" }, { "manifestDestinations", path("manifest-copy") },
            { "destinations", QJsonObject { { "*", path("copy") } } },
        }).toJson());
        COpyController controller;
        QVERIFY(controller.importPresets(path("cluster.json")));
        controller.applyPreset("test-cluster");
        QVERIFY(controller.loadManifest(manifest()));
        QCOMPARE(controller.plannedCopies().size(), 1);
        QCOMPARE(controller.plannedCopies().first().toMap().value("destination").toString(), QDir::toNativeSeparators(path("manifest-copy")));
        // An output's own destinations still come first.
        controller.addDestination("output", path("own"));
        QCOMPARE(controller.plannedCopies().first().toMap().value("destination").toString(), QDir::toNativeSeparators(path("own")));
    }

    void changedLibraryClustersAreAppliedUnlessEdited()
    {
        writeFile(path("source/example.txt"), "source");
        const auto writeCluster = [this](const QString &manifestDestination) {
            writeFile(path("clusters/library.json"), QJsonDocument(QJsonObject {
                { "name", "library" }, { "manifestDestinations", manifestDestination },
                { "destinations", QJsonObject { { "*", path("copy") } } },
            }).toJson());
        };
        const auto destination = [](const COpyController &controller) {
            return controller.plannedCopies().first().toMap().value("destination").toString();
        };
        writeCluster(path("first"));
        {
            COpyController controller;
            controller.applyPreset("library");
            QVERIFY(controller.loadManifest(manifest()));
            QCOMPARE(destination(controller), QDir::toNativeSeparators(path("first")));
            writeCluster(path("second"));
            controller.reloadLibrary();
            QCOMPARE(destination(controller), QDir::toNativeSeparators(path("second")));
            controller.addDestination("output", path("own"));
        }
        // On the next start the changed file is applied unless the working mapping was edited.
        writeCluster(path("third"));
        COpyController controller;
        QVERIFY(controller.loadManifest(manifest()));
        QCOMPARE(destination(controller), QDir::toNativeSeparators(path("own")));
        controller.applyPreset("library");
        QCOMPARE(destination(controller), QDir::toNativeSeparators(path("third")));
    }

    void disabledPlannedCopiesAreSkipped()
    {
        writeFile(path("source/example.txt"), "source");
        COpyController controller;
        QVERIFY(controller.loadManifest(manifest()));
        controller.addDestination("*", path("first"));
        controller.addDestination("*", path("second"));
        QCOMPARE(readyCopies(controller), 2);
        const QString key = controller.plannedCopies().first().toMap().value("key").toString();
        controller.setPlannedCopyEnabled(key, false);
        QCOMPARE(readyCopies(controller), 1);
        QVERIFY(controller.plannedCopies().first().toMap().value("disabled").toBool());
        controller.setPlannedCopyEnabled(key, true);
        QCOMPARE(readyCopies(controller), 2);
        controller.setPlannedCopyEnabled(key, false);

        controller.startCopy();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        QCOMPARE(controller.tasks().size(), 1);
        QVERIFY(!QDir(path("first")).exists());
        QCOMPARE(inventory(path("second")), inventory(path("source")));
        // Still disabled when the finished source is ready to copy again.
        QCOMPARE(readyCopies(controller), 1);
    }

    void finishedSourcesCanBeCopiedAgain()
    {
        writeFile(path("source/example.txt"), "source");
        COpyController controller;
        QVERIFY(controller.loadManifest(manifest()));
        controller.addDestination("*", path("copy"));
        QCOMPARE(readyCopies(controller), 1);
        controller.startCopy();
        QCOMPARE(controller.readyManifestCount(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        QCOMPARE(controller.readyManifestCount(), 1);
        QCOMPARE(readyCopies(controller), 1);

        writeFile(path("source/added.txt"), "added");
        controller.startCopy();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.failedTaskCount(), 0);
        QCOMPARE(inventory(path("copy")), inventory(path("source")));
        controller.clearFinished();
        QVERIFY(controller.tasks().isEmpty());
        QCOMPARE(controller.manifests().size(), 1);
    }

    static QVariantMap targetAt(const COpyController &controller, const QString &node, bool source = false)
    {
        for (const QVariant &entry : controller.targets()) {
            const QVariantMap target = entry.toMap();
            if (target.value("source").toBool() == source && (source || target.value("node").toString() == node)) {
                return target;
            }
        }
        return QVariantMap();
    }

    void checkAndDeleteOnNodesAndSource()
    {
        writeFile(path("source/a.txt"), "a");
        writeFile(path("source/sub/b.txt"), "bb");
        writeFile(path("copy/one/old.txt"), "old");
        writeFile(path("copy/one/sub/older.txt"), "older");
        writeFile(path("external/foreign.txt"), "foreign");
        QCOMPARE(junction(path("copy/one/linked"), path("external")), 0);
        writeFile(path("nodes/one.json"), R"({"name":"one","ip":"127.0.0.1"})");
        writeFile(path("nodes/two.json"), R"({"name":"two","ip":"127.0.0.2"})");
        writeFile(path("cluster.json"), R"({"name":"test-cluster","nodes":{"*":"one","other":"two"}})");
        COpyController controller;
        QVERIFY(controller.importPresets(path("cluster.json")));
        controller.applyPreset("test-cluster");
        controller.setSamePath(false);
        controller.addDestination("*", path("copy/{node}"));
        QVERIFY(controller.addSource(path("source")));
        QCOMPARE(readyCopies(controller), 2);
        QVERIFY(!controller.targetsStale());

        controller.checkTargets();
        QVERIFY(controller.targetsBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.targetsBusy(), 30000);
        QCOMPARE(controller.targets().size(), 3);
        const QVariantMap source = targetAt(controller, QString(), true);
        QCOMPARE(source.value("state").toString(), QString("present"));
        QCOMPARE(source.value("fileCount").toInt(), 2);
        QCOMPARE(source.value("totalBytes").toLongLong(), 3);
        const QVariantMap one = targetAt(controller, "one");
        QCOMPARE(one.value("state").toString(), QString("present"));
        QCOMPARE(one.value("fileCount").toInt(), 2);
        // sub and the junction, which is not followed.
        QCOMPARE(one.value("folderCount").toInt(), 2);
        QCOMPARE(targetAt(controller, "two").value("state").toString(), QString("missing"));
        for (const QVariant &node : controller.checkedNodes()) {
            QCOMPARE(node.toMap().value("state").toString(), QString("reachable"));
        }

        // Only what was chosen is deleted; the junction is removed, not followed.
        controller.deleteTargets({ one.value("id") });
        QVERIFY(!controller.openPath(path("external")));
        QCOMPARE(controller.manifests().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.targetsBusy(), 30000);
        QCOMPARE(targetAt(controller, "one").value("state").toString(), QString("missing"));
        QVERIFY(!QFileInfo::exists(path("copy/one")));
        junctions.removeOne(path("copy/one/linked"));
        QVERIFY(QFileInfo::exists(path("external/foreign.txt")));
        QVERIFY(QFileInfo::exists(path("source/a.txt")));

        // Nothing is deleted while copying.
        controller.startCopy();
        controller.deleteTargets({ source.value("id") });
        QVERIFY(!controller.targetsBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QVERIFY(QFileInfo::exists(path("source/a.txt")));
        QCOMPARE(controller.failedTaskCount(), 0);
        controller.clearFinished();

        controller.checkTargets();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.targetsBusy(), 30000);
        QVariantList ids;
        for (const QVariant &entry : controller.targets()) {
            QCOMPARE(entry.toMap().value("state").toString(), QString("present"));
            ids << entry.toMap().value("id");
        }
        controller.deleteTargets(ids);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.targetsBusy(), 30000);
        for (const QVariant &entry : controller.targets()) {
            QCOMPARE(entry.toMap().value("state").toString(), QString("missing"));
        }
        QVERIFY(!QFileInfo::exists(path("copy/one")));
        QVERIFY(!QFileInfo::exists(path("copy/two")));
        QVERIFY(!QFileInfo::exists(path("source")));
        // A deleted source is no longer listed.
        QVERIFY(controller.manifests().isEmpty());
    }

    void deletingACopiedFileKeepsTheRestOfItsFolder()
    {
        writeFile(path("source/movie.mp4"), "movie");
        writeFile(path("copy/movie.mp4"), "old movie");
        writeFile(path("copy/keep.txt"), "keep");
        writeFile(path("cluster.json"), QJsonDocument(QJsonObject {
            { "name", "test-cluster" }, { "nodes", QJsonObject { { "*", "remote" } } },
            { "destinations", QJsonObject { { "*", path("copy") } } },
        }).toJson());
        COpyController controller;
        QVERIFY(controller.importPresets(path("cluster.json")));
        controller.applyPreset("test-cluster");
        controller.setSamePath(false);
        QVERIFY(controller.addSource(path("source/movie.mp4")));
        controller.checkTargets();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.targetsBusy(), 30000);
        const QVariantMap copy = targetAt(controller, "remote");
        QCOMPARE(copy.value("state").toString(), QString("present"));
        QCOMPARE(copy.value("fileCount").toInt(), 1);
        QCOMPARE(copy.value("totalBytes").toLongLong(), 9);
        QVERIFY(!copy.value("wholeFolder").toBool());
        controller.deleteTargets({ copy.value("id") });
        QTRY_VERIFY_WITH_TIMEOUT(!controller.targetsBusy(), 30000);
        QCOMPARE(targetAt(controller, "remote").value("state").toString(), QString("missing"));
        QVERIFY(!QFileInfo::exists(path("copy/movie.mp4")));
        QVERIFY(QFileInfo::exists(path("copy/keep.txt")));
        // The source was not chosen, so it stays.
        QVERIFY(QFileInfo::exists(path("source/movie.mp4")));
        QCOMPARE(controller.manifests().size(), 1);

        // A changed plan asks for another check.
        controller.addDestination("*", path("second"));
        QVERIFY(controller.targetsStale());
    }

    void stoppedProcessesAreKeptUntilTheyExit()
    {
        writeFile(path("source/example.txt"), "source");
        COpyController controller;
        QVERIFY(controller.loadManifest(manifest()));
        controller.addDestination("*", path("copy"));
        controller.startCopy();
        controller.stopCopy();
        QCOMPARE(controller.finishedTaskCount(), 0);
        controller.clearFinished();
        QCOMPARE(controller.tasks().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 30000);
        QCOMPARE(controller.finishedTaskCount(), 1);
        controller.clearFinished();
        QVERIFY(controller.tasks().isEmpty());
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    CopyControllerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "copycontroller_tests.moc"
