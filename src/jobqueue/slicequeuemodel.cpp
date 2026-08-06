/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "slicequeuemodel.h"

void SliceQueueModel::setSelectedIndex(int index)
{
    if (m_selectedIndex == index) {
        return;
    }
    m_selectedIndex = index;
    Q_EMIT selectedIndexChanged();
}

QHash<int, QByteArray> SliceQueueModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "jobName";
    roles[StatusRole] = "status";
    return roles;
}