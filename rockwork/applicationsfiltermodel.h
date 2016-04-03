#ifndef APPLICATIONSFILTERMODEL_H
#define APPLICATIONSFILTERMODEL_H

#include <QSortFilterProxyModel>

class ApplicationsModel;
class AppItem;

class ApplicationsFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(ApplicationsModel* model READ appsModel WRITE setAppsModel NOTIFY appsModelChanged)
    Q_PROPERTY(bool showWatchApps READ showWatchApps WRITE setShowWatchApps NOTIFY showWatchAppsChanged)
    Q_PROPERTY(bool showWatchFaces READ showWatchFaces WRITE setShowWatchFaces NOTIFY showWatchFacesChanged)
    Q_PROPERTY(bool sortByGroupId READ sortByGroupId WRITE setSortByGroupId NOTIFY sortByGroupIdChanged)
    Q_PROPERTY(bool showCategories READ showCategories WRITE setShowCategories NOTIFY showCategoriesChanged)
    Q_PROPERTY(bool filterCompanion READ filterCompanion WRITE setFilterCompanion NOTIFY filterCompanionChanged)

public:
    ApplicationsFilterModel(QObject *parent = nullptr);

    ApplicationsModel *appsModel() const;
    void setAppsModel(ApplicationsModel *model);

    bool showWatchApps() const;
    void setShowWatchApps(bool showWatchApps);

    bool showWatchFaces() const;
    void setShowWatchFaces(bool showWatchFaces);

    bool sortByGroupId() const;
    void setSortByGroupId(bool sortByGroupId);

    bool showCategories() const;
    void setShowCategories(bool showCategories);

    bool filterCompanion() const;
    void setFilterCompanion(bool filter);

    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;
    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override;

    Q_INVOKABLE AppItem *get(int index) const;

    Q_INVOKABLE void move(int from, int to);
signals:
    void appsModelChanged();
    void showWatchAppsChanged();
    void showWatchFacesChanged();
    void sortByGroupIdChanged();
    void showCategoriesChanged();
    void filterCompanionChanged();

public slots:

private:
    ApplicationsModel *m_appsModel;

    bool m_showWatchApps = true;
    bool m_showWatchFaces = true;
    bool m_sortByGroupId = true;
    bool m_showCategories = false;
    bool m_filterCompanion = true;
};

#endif // APPLICATIONSFILTERMODEL_H
