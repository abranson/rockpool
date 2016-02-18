#include "applicationsfiltermodel.h"
#include "applicationsmodel.h"

ApplicationsFilterModel::ApplicationsFilterModel(QObject *parent):
    QSortFilterProxyModel(parent)
{
    sort(0);
}

ApplicationsModel *ApplicationsFilterModel::appsModel() const
{
    return m_appsModel;
}

void ApplicationsFilterModel::setAppsModel(ApplicationsModel *model)
{
    if (m_appsModel != model) {
        m_appsModel = model;
        setSourceModel(m_appsModel);
        emit appsModelChanged();
    }
}

bool ApplicationsFilterModel::showWatchApps() const
{
    return m_showWatchApps;
}

void ApplicationsFilterModel::setShowWatchApps(bool showWatchApps)
{
    if (m_showWatchApps != showWatchApps) {
        m_showWatchApps = showWatchApps;
        emit showWatchAppsChanged();
        invalidateFilter();
    }
}

bool ApplicationsFilterModel::showWatchFaces() const
{
    return m_showWatchFaces;
}

void ApplicationsFilterModel::setShowWatchFaces(bool showWatchFaces)
{
    if (m_showWatchFaces != showWatchFaces) {
        m_showWatchFaces = showWatchFaces;
        emit showWatchFacesChanged();
        invalidateFilter();
    }
}

bool ApplicationsFilterModel::sortByGroupId() const
{
    return m_sortByGroupId;
}

void ApplicationsFilterModel::setSortByGroupId(bool sortByGroupId)
{
    if (m_sortByGroupId != sortByGroupId) {
        m_sortByGroupId = sortByGroupId;
        emit sortByGroupIdChanged();
        sort(0);
    }
}

bool ApplicationsFilterModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
    Q_UNUSED(source_parent)
    AppItem *item = m_appsModel->get(source_row);
    if (m_showWatchApps && !item->isWatchFace()) {
        return true;
    }
    if (m_showWatchFaces && item->isWatchFace()) {
        return true;
    }
    return false;
}

bool ApplicationsFilterModel::lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const
{
    AppItem *leftItem = m_appsModel->get(source_left.row());
    AppItem *rightItem = m_appsModel->get(source_right.row());

    if (m_sortByGroupId && leftItem->groupId() != rightItem->groupId()) {
        return leftItem->groupId() < rightItem->groupId();
    }

    return QSortFilterProxyModel::lessThan(source_left, source_right);
}

AppItem* ApplicationsFilterModel::get(int index) const
{
    return m_appsModel->get(mapToSource(this->index(index, 0)).row());
}

void ApplicationsFilterModel::move(int from, int to)
{
    QModelIndex sourceFrom = mapToSource(index(from, 0));
    QModelIndex sourceTo = mapToSource(index(to, 0));
    m_appsModel->move(sourceFrom.row(), sourceTo.row());
}

