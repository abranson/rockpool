#ifndef APPDOWNLOADER_H
#define APPDOWNLOADER_H

#include <QObject>
#include <QMap>

class QNetworkAccessManager;

class AppDownloader : public QObject
{
    Q_OBJECT
public:
    explicit AppDownloader(const QString &storagePath, QObject *parent = 0);

public slots:
    void downloadApp(const QString &id);

signals:
    void downloadFinished(const QString &id);

private slots:
    void appJsonFetched();
    void packageFetched();

private:
    void fetchPackage(const QString &url, const QString &storeId);

    QNetworkAccessManager *m_nam;
    QString m_storagePath;
};

#endif // APPDOWNLOADER_H
