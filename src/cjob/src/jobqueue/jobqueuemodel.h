/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef JOBQUEUEMODEL_H
#define JOBQUEUEMODEL_H

#include <QAbstractListModel>
#include <QStringList>

class JobQueueModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QStringList jobNames READ jobNames WRITE setJobNames NOTIFY jobNamesChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        StatusRole
    };
    Q_ENUM(Role)

    explicit JobQueueModel(QObject *parent = nullptr);

    int count() const { return m_jobNames.size(); }
    QStringList jobNames() const { return m_jobNames; }
    void setJobNames(const QStringList &names);

    // QAbstractListModel interface
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    bool removeRows(int row, int count, const QModelIndex &parent = {}) override;
    bool insertRows(int row, int count, const QModelIndex &parent = {}) override;
    QHash<int, QByteArray> roleNames() const override;

    // Drag and drop support
    Qt::DropActions supportedDragActions() const override { return Qt::MoveAction; }
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

    // Reorder methods
    Q_INVOKABLE void moveRow(int from, int to);
    Q_INVOKABLE void swapRows(int first, int second);

Q_SIGNALS:
    void countChanged();
    void jobNamesChanged();
    void rowsMoved(int start, int end, int destination);

private:
    QStringList m_jobNames;
};

#endif // JOBQUEUEMODEL_H