/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SPLITAPPLICATION_H
#define CSLICE_SPLITAPPLICATION_H

#include "slicecontroller.h"

#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <QUrl>

class QGuiApplication;
class KAboutData;
class QQmlApplicationEngine;

class SliceApplication : public QObject {
    Q_OBJECT
    Q_PROPERTY(SliceController *controller READ controller CONSTANT)
    Q_PROPERTY(QString ffmpegVersion READ ffmpegVersion CONSTANT)
    Q_PROPERTY(QString ffmpegLibraries READ ffmpegLibraries CONSTANT)

public:
    explicit SliceApplication(int &argc, char **argv);
    ~SliceApplication() override;

    int run();
    SliceController *controller();
    QString ffmpegVersion() const;
    QString ffmpegLibraries() const;

    Q_INVOKABLE QString version() const;
    Q_INVOKABLE QUrl pathToUrl(const QString &path) const;
    Q_INVOKABLE QString urlToPath(const QUrl &url) const;
    Q_INVOKABLE QString sliceDataPath(const QString &relativePath) const;
    Q_INVOKABLE QString readTextFile(const QString &path) const;

private:
    QGuiApplication *m_app = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;
    KAboutData *m_aboutData = nullptr;
    SliceController m_controller;
};

#endif // CSLICE_SLICEAPPLICATION_H
