#include "screenshotmodel.h"

#include <QDebug>

ScreenshotModel::ScreenshotModel(QObject *parent):
    QAbstractListModel(parent)
{
}

int ScreenshotModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_files.count();
}

QVariant ScreenshotModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case RoleFileName:
        return m_files.at(index.row());
    }
    return QVariant();
}

QHash<int, QByteArray> ScreenshotModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(RoleFileName, "filename");
    return roles;
}

QString ScreenshotModel::get(int index) const
{
    if (index >= 0 && index < m_files.count()) {
        return m_files.at(index);
    }
    return QString();
}

QString ScreenshotModel::latestScreenshot() const
{
    return get(0);
}

void ScreenshotModel::clear()
{
    beginResetModel();
    m_files.clear();
    endResetModel();
}

void ScreenshotModel::insert(const QString &filename)
{
    qDebug() << "should insert filename" << filename;
    if (!m_files.contains(filename)) {
        beginInsertRows(QModelIndex(), 0, 0);
        m_files.prepend(filename);
        endInsertRows();
        emit latestScreenshotChanged();
    }
}

void ScreenshotModel::remove(const QString &filename)
{
    if (m_files.contains(filename)) {
        int idx = m_files.indexOf(filename);
        beginRemoveRows(QModelIndex(), idx, idx);
        m_files.removeOne(filename);
        endRemoveRows();
    }
}
