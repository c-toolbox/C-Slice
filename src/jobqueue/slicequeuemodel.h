/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SLICEQUEUEMODEL_H
#define CSLICE_SLICEQUEUEMODEL_H

#include "jobqueuemodel.h"

class SliceQueueModel : public JobQueueModel {
    Q_OBJECT
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)

public:
    explicit SliceQueueModel(QObject *parent = nullptr) : JobQueueModel(parent) {}

    int selectedIndex() const { return m_selectedIndex; }
    void setSelectedIndex(int index);

Q_SIGNALS:
    void selectedIndexChanged();

private:
    int m_selectedIndex = -1;

    // Override to allow proper Qt meta-object system integration
    QHash<int, QByteArray> roleNames() const override;
};

#endif // CSLICE_SLICEQUEUEMODEL_H