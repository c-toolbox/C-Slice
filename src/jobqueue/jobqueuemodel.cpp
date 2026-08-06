/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jobqueuemodel.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <algorithm>

JobQueueModel::JobQueueModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int JobQueueModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_jobNames.size();
}

QVariant JobQueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_jobNames.size()) {
        return QVariant();
    }

    switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return m_jobNames.at(index.row());
        default:
            return QVariant();
    }
}

bool JobQueueModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= m_jobNames.size()) {
        return false;
    }

    if (role == Qt::EditRole || role == NameRole) {
        const QString newName = value.toString();
        if (m_jobNames[index.row()] != newName) {
            m_jobNames[index.row()] = newName;
            Q_EMIT dataChanged(index, index, {role});
            return true;
        }
    }
    return false;
}

bool JobQueueModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (row < 0 || row >= m_jobNames.size() || count <= 0) {
        return false;
    }

    const int end = qMin(row + count, m_jobNames.size());
    beginRemoveRows(parent, row, end - 1);
    m_jobNames.removeAt(row);
    endRemoveRows();
    Q_EMIT jobNamesChanged();
    Q_EMIT countChanged();
    return true;
}

bool JobQueueModel::insertRows(int row, int count, const QModelIndex &parent)
{
    if (row < 0 || row > m_jobNames.size() || count <= 0) {
        return false;
    }

    beginInsertRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_jobNames.insert(row + i, QString());
    }
    endInsertRows();
    Q_EMIT jobNamesChanged();
    Q_EMIT countChanged();
    return true;
}

QHash<int, QByteArray> JobQueueModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "jobName";
    roles[StatusRole] = "status";
    return roles;
}

Qt::ItemFlags JobQueueModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    // Enable ItemIsSelectable, ItemIsDragEnabled for drag support
    // ItemIsDropEnabled is set on all items to allow dropping anywhere
    return QAbstractListModel::flags(index) | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

// supportedDragActions and supportedDropActions are defined inline in header

QStringList JobQueueModel::mimeTypes() const
{
    return QStringList(QStringLiteral("application/x-jobqueueitem"));
}

QMimeData *JobQueueModel::mimeData(const QModelIndexList &indexes) const
{
    QMimeData *mimeData = new QMimeData();
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);

    for (const QModelIndex &index : indexes) {
        if (index.isValid() && index.row() < m_jobNames.size()) {
            stream << index.row();
        }
    }

    mimeData->setData(mimeTypes().first(), encoded);
    return mimeData;
}

bool JobQueueModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    if (!data->hasFormat(mimeTypes().first()) || action != Qt::MoveAction) {
        return false;
    }

    QByteArray encoded = data->data(mimeTypes().first());
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QVector<int> rows;
    while (!stream.atEnd()) {
        int r;
        stream >> r;
        if (r >= 0 && r < m_jobNames.size()) {
            rows.append(r);
        }
    }

    if (rows.isEmpty()) {
        return false;
    }

    // Read single row for simplicity (single item drag)
    const int fromRow = rows.first();
    
    if (fromRow < 0 || fromRow >= m_jobNames.size() || row == fromRow) {
        return false;
    }

    const int clampedTo = qBound(0, row, (int)m_jobNames.size());

    if (fromRow == clampedTo) {
        return false;
    }

    // Emit rowsMoved signal so backend can perform its physical re-order
    Q_EMIT rowsMoved(fromRow, fromRow, clampedTo);
    
    // Also physically reorder the model to reflect the change immediately in the UI.
    beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), clampedTo > fromRow ? qMin(clampedTo, m_jobNames.size() - 1) : qMax(0, clampedTo));
    const QString job = m_jobNames.takeAt(fromRow);
    int insertPos = clampedTo;
    if (clampedTo > fromRow) {
        insertPos = qMin(clampedTo, m_jobNames.size());
    }
    m_jobNames.insert(insertPos, job);
    endMoveRows();
    
    return true;
}

void JobQueueModel::moveRow(int from, int to)
{
    if (from < 0 || from >= m_jobNames.size()) {
        return;
    }

    // Clamp 'to' to valid range of the CURRENT list size (before removal)
    const int clampedTo = qBound(0, to, m_jobNames.size());
    
    if (from == clampedTo) {
        return;
    }

    // First emit rowsMoved signal so backend can perform its physical re-order
    Q_EMIT rowsMoved(from, from, clampedTo);
    
    // Then physically reorder the model to reflect the change immediately in the UI.
    // The backend will also reorder and sync back via setJobNames(), but we need to
    // update here too so the ListView doesn't go out of sync after a drag-drop.
    // Using beginMoveRows/endMoveRows for proper model notification.
    int actualTo = clampedTo;
    if (clampedTo > from) {
        actualTo = qMin(clampedTo, m_jobNames.size() - 1);
    } else {
        actualTo = qMax(0, clampedTo);
    }
    
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), actualTo);
    const QString job = m_jobNames.takeAt(from);
    if (actualTo >= m_jobNames.size()) {
        m_jobNames.append(job);
    } else {
        m_jobNames.insert(actualTo, job);
    }
    endMoveRows();
}

void JobQueueModel::setJobNames(const QStringList &names)
{
    if (names == m_jobNames) {
        return;
    }
    beginResetModel();
    m_jobNames = names;
    endResetModel();
    Q_EMIT jobNamesChanged();
}

void JobQueueModel::swapRows(int first, int second)
{
    if (first < 0 || first >= m_jobNames.size() || 
        second < 0 || second >= m_jobNames.size() || first == second) {
        return;
    }

    beginResetModel();
    std::iter_swap(m_jobNames.begin() + first, m_jobNames.begin() + second);
    endResetModel();
    
    Q_EMIT rowsMoved(first, first, second);
    Q_EMIT jobNamesChanged();
}