#ifndef APPSTORECLIENT_H
#define APPSTORECLIENT_H

#include <QObject>

class QUrl;
class QNetworkAccessManager;
class ApplicationsModel;
class AppItem;

class AppStoreClient : public QObject
{
    Q_OBJECT
    Q_ENUMS(Type Source)
    Q_PROPERTY(Source source MEMBER m_source NOTIFY sourceChanged)
    Q_PROPERTY(ApplicationsModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    Q_PROPERTY(QString hardwarePlatform READ hardwarePlatform WRITE setHardwarePlatform NOTIFY hardwarePlatformChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool enableCategories READ enableCategories WRITE setEnableCategories NOTIFY enableCategoriesChanged)

public:
    enum Source { Rebble, CoreDevices };
    enum Type {
        TypeWatchapp,
        TypeWatchface
    };

    explicit AppStoreClient(QObject *parent = 0);

    ApplicationsModel *model() const;
    void setModel(ApplicationsModel *p_model);

    int limit() const;
    void setLimit(int limit);

    QString hardwarePlatform() const;
    void setHardwarePlatform(const QString &hardwarePlatform);

    void setEnableCategories(const bool enable);
    bool enableCategories() const;

    bool busy() const;

signals:
    void sourceChanged();
    void limitChanged();
    void hardwarePlatformChanged();
    void busyChanged();
    void enableCategoriesChanged();
    void modelChanged();

public slots:
    void fetchHome(Type type);
    void fetchLink(const QString &link);
    void fetchInstalledListing(AppItem *item, const QString &store, bool openPage);

    void fetchAppDetails(const QString &appId);

    void search(const QString &searchString, Type type, int page);
    void search(const QString &searchString, Type type);

private:
    void fetchListing(const QUrl &url, Source source);
    AppItem *parseAppItem(const QVariantMap &map, Source source);
    void setBusy(bool busy);

private:
    QNetworkAccessManager *m_nam;
    ApplicationsModel *m_model;
    ApplicationsModel *f_model = 0;
    Source m_source = Rebble;
    quint64 m_catalogueEpoch = 0;
    quint64 m_homeRequest = 0;
    int m_limit = 20;
    QString m_hardwarePlatform;
    bool m_busy = false;
    bool m_enableCategories = false;
};

#endif // APPSTORECLIENT_H
