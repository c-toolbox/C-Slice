/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_MPVPREVIEWITEM_H
#define CSLICE_MPVPREVIEWITEM_H

#include <QPoint>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSize>
#include <QString>

class QTimer;

namespace CSlice {

class MpvPreviewRenderer;

// Qt Quick item that displays a video through its own private MpvVideo
// render-to-texture pipeline. Each item owns a dedicated mpv instance, so the
// preview never interferes with an ongoing slice operation.
//
// Modelled after C-Play's LayerQtItem: the item lives on the GUI thread and
// exposes properties, while the actual OpenGL work happens on the Qt Quick
// render thread inside MpvPreviewRenderer.
class MpvPreviewItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int frame READ frame WRITE setFrame NOTIFY frameChanged)

public:
    MpvPreviewItem();
    ~MpvPreviewItem() override;

    QString source() const;
    void setSource(const QString &source);

    int frame() const;
    void setFrame(int frame);

Q_SIGNALS:
    void sourceChanged();
    void frameChanged();

private Q_SLOTS:
    void handleWindowChanged(QQuickWindow *window);
    void sync();
    void cleanup();

private:
    void releaseResources() override;

    QString m_source;
    int m_frame = 0;

    MpvPreviewRenderer *m_renderer = nullptr;
    QTimer *m_timer = nullptr;
};

} // namespace CSlice

#endif // CSLICE_MPVPREVIEWITEM_H
